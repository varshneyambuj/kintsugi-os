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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Jérôme Duval (korli@users.berlios.de)
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Artur Wyszynski
 *       Rene Gollent (rene@gollent.com)
 */


/**
 * @file TabView.cpp
 * @brief Implementation of BTabView and BTab for tabbed panel interfaces
 *
 * BTabView manages a row of tab labels (BTab objects) above a shared content
 * area. Clicking a tab selects it and shows its associated view. BTab holds
 * the label and optional view, and can be subclassed to customize tab
 * appearance and behavior.
 *
 * @see BView, BTab, BControlLook
 */


#include <TabView.h>
#include <TabViewPrivate.h>

#include <new>

#include <math.h>
#include <string.h>

#include <CardLayout.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <LayoutUtils.h>
#include <List.h>
#include <Message.h>
#include <PropertyInfo.h>
#include <Rect.h>
#include <Region.h>
#include <String.h>
#include <Window.h>

#include <binary_compatibility/Support.h>
#include <private/libroot/libroot_private.h>


/** @brief Scripting property table exposing the "Selection" property via the BeOS scripting protocol. */
static property_info sPropertyList[] = {
	{
		"Selection",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_INT32_TYPE }
	},

	{ 0 }
};


/**
 * @brief Returns whether @a childView is managed by a BCardLayout.
 *
 * @param childView The view to test.
 * @return @c true if the view's parent uses a BCardLayout, @c false otherwise.
 */
static bool
IsLayouted(BView* childView)
{
	BView* container = childView->Parent();
	if (container != NULL)
		return dynamic_cast<BCardLayout*>(container->GetLayout()) != NULL;
	return false;
}


/**
 * @brief Constructs a BTab optionally associated with a content view.
 *
 * If @a contentsView is non-NULL its BView::Name() is used as the initial
 * tab label.
 *
 * @param contentsView The view to display when this tab is selected, or NULL.
 */
BTab::BTab(BView* contentsView)
	:
	fEnabled(true),
	fSelected(false),
	fFocus(false),
	fView(contentsView)
{
	fTabView = NULL;
	if (fView != NULL)
		fLabel = fView->Name();
}


/**
 * @brief Constructs a BTab from an archived BMessage.
 *
 * Restores the enabled state from the "_disable" field if present.
 *
 * @param archive The BMessage previously produced by BTab::Archive().
 */
BTab::BTab(BMessage* archive)
	:
	BArchivable(archive),
	fSelected(false),
	fFocus(false),
	fView(NULL)
{
	fTabView = NULL;

	bool disable;

	if (archive->FindBool("_disable", &disable) != B_OK)
		SetEnabled(true);
	else
		SetEnabled(!disable);
}


/**
 * @brief Destructs the BTab, removing and deleting its associated view.
 *
 * If the tab is currently selected, the view is first removed from its parent
 * before being deleted.
 */
BTab::~BTab()
{
	if (fView == NULL)
		return;

	if (fSelected)
		fView->RemoveSelf();

	delete fView;
}


/**
 * @brief Creates a new BTab from an archived BMessage.
 *
 * @param archive The BMessage to instantiate from.
 * @return A new BTab, or NULL if the archive is invalid.
 */
BArchivable*
BTab::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BTab"))
		return new BTab(archive);

	return NULL;
}


/**
 * @brief Archives the BTab into a BMessage.
 *
 * Stores the disabled state (when false) under the "_disable" key.
 *
 * @param data  The BMessage to archive into.
 * @param deep  If @c true, child objects are also archived (unused here).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BTab::Archive(BMessage* data, bool deep) const
{
	status_t result = BArchivable::Archive(data, deep);
	if (result != B_OK)
		return result;

	if (!fEnabled)
		result = data->AddBool("_disable", false);

	return result;
}


/**
 * @brief Dispatches a perform code to the base class.
 *
 * @param d   The perform code.
 * @param arg Opaque argument whose meaning depends on @a d.
 * @return B_OK on success, or an error code.
 */
status_t
BTab::Perform(uint32 d, void* arg)
{
	return BArchivable::Perform(d, arg);
}


/**
 * @brief Returns the tab's display label.
 *
 * Under the GCC2/BeOS ABI the label is sourced from the associated view's
 * name; under the Haiku ABI it is an independent string.
 *
 * @return The label string, or NULL if none is set.
 */
const char*
BTab::Label() const
{
#ifdef __HAIKU_BEOS_COMPATIBLE
	if (__gABIVersion >= B_HAIKU_ABI_GCC_2_HAIKU)
		return fLabel;

	if (fView != NULL)
		return fView->Name();

	return NULL;
#else
	return fLabel;
#endif
}


/**
 * @brief Sets the tab's display label and invalidates the parent tab view.
 *
 * Has no effect if @a label is NULL or no view has been associated with the tab.
 *
 * @param label The new label string.
 */
void
BTab::SetLabel(const char* label)
{
	if (label == NULL || fView == NULL)
		return;

#ifdef __HAIKU_BEOS_COMPATIBLE
	if (__gABIVersion < B_HAIKU_ABI_GCC_2_HAIKU)
		fView->SetName(label);
#endif
	fLabel = label;

	if (fTabView != NULL)
		fTabView->Invalidate();
}


/**
 * @brief Returns whether this tab is currently selected.
 *
 * @return @c true if selected, @c false otherwise.
 */
bool
BTab::IsSelected() const
{
	return fSelected;
}


/**
 * @brief Marks this tab as selected and shows its associated view.
 *
 * If the owner uses no layout, the view is added as a child of @a owner.
 * If a BCardLayout is in use, visibility is managed by the layout instead.
 *
 * @param owner The container view that hosts tab content (typically
 *              BTabView::ContainerView()).
 */
void
BTab::Select(BView* owner)
{
	fSelected = true;

	if (owner == NULL || fView == NULL)
		return;

	// NOTE: Views are not added/removed, if there is layout,
	// they are made visible/invisible in that case.
	if (owner->GetLayout() == NULL && fView->Parent() == NULL)
		owner->AddChild(fView);
}


/**
 * @brief Marks this tab as deselected and hides its associated view.
 *
 * If the view is not managed by a layout, it is removed from its parent.
 */
void
BTab::Deselect()
{
	if (fView != NULL) {
		// NOTE: Views are not added/removed, if there is layout,
		// they are made visible/invisible in that case.
		if (!IsLayouted(fView))
			fView->RemoveSelf();
	}

	fSelected = false;
}


/**
 * @brief Enables or disables the tab.
 *
 * @param enable @c true to enable the tab, @c false to disable it.
 */
void
BTab::SetEnabled(bool enable)
{
	fEnabled = enable;
}


/**
 * @brief Returns whether the tab is currently enabled.
 *
 * @return @c true if enabled, @c false if disabled.
 */
bool
BTab::IsEnabled() const
{
	return fEnabled;
}


/**
 * @brief Sets the keyboard-focus state of the tab.
 *
 * @param focus @c true to give this tab keyboard focus, @c false to remove it.
 * @see BTab::IsFocus()
 */
void
BTab::MakeFocus(bool focus)
{
	fFocus = focus;
}


/**
 * @brief Returns whether this tab currently has keyboard focus.
 *
 * @return @c true if the tab has focus, @c false otherwise.
 */
bool
BTab::IsFocus() const
{
	return fFocus;
}


/**
 * @brief Associates a new content view with the tab, replacing any existing one.
 *
 * The old view is removed from the hierarchy and deleted. The label is updated
 * to the new view's name. If this tab is currently selected, the new view is
 * shown immediately.
 *
 * @param view The new content view; must be non-NULL and different from the
 *             current view.
 */
void
BTab::SetView(BView* view)
{
	if (view == NULL || fView == view)
		return;

	if (fView != NULL) {
		fView->RemoveSelf();
		delete fView;
	}
	fView = view;
	fLabel = fView->Name();

	if (fTabView != NULL && fSelected) {
		Select(fTabView->ContainerView());
		fTabView->Invalidate();
	}
}


/**
 * @brief Returns the content view associated with this tab.
 *
 * @return The associated BView, or NULL if none has been set.
 */
BView*
BTab::View() const
{
	return fView;
}


/**
 * @brief Draws the keyboard-navigation focus indicator for this tab.
 *
 * Draws a short underline beneath (or beside, for vertical tab sides) the
 * tab label using the system keyboard-navigation color.
 *
 * @param owner The BView on which to draw (typically the BTabView).
 * @param frame The bounding rectangle of this tab as returned by
 *              BTabView::TabFrame().
 */
void
BTab::DrawFocusMark(BView* owner, BRect frame)
{
	float width = owner->StringWidth(Label());

	owner->SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));

	float offset = IsSelected() ? 3 : 2;
	switch (fTabView->TabSide()) {
		case BTabView::kTopSide:
			owner->StrokeLine(BPoint((frame.left + frame.right - width) / 2.0,
					frame.bottom - offset),
				BPoint((frame.left + frame.right + width) / 2.0,
					frame.bottom - offset));
			break;
		case BTabView::kBottomSide:
			owner->StrokeLine(BPoint((frame.left + frame.right - width) / 2.0,
					frame.top + offset),
				BPoint((frame.left + frame.right + width) / 2.0,
					frame.top + offset));
			break;
		case BTabView::kLeftSide:
			owner->StrokeLine(BPoint(frame.right - offset,
					(frame.top + frame.bottom - width) / 2.0),
				BPoint(frame.right - offset,
					(frame.top + frame.bottom + width) / 2.0));
			break;
		case BTabView::kRightSide:
			owner->StrokeLine(BPoint(frame.left + offset,
					(frame.top + frame.bottom - width) / 2.0),
				BPoint(frame.left + offset,
					(frame.top + frame.bottom + width) / 2.0));
			break;
	}
}


/**
 * @brief Draws the tab label centered within @a frame.
 *
 * Applies the appropriate rotation transform for left/right tab sides and
 * delegates to BControlLook::DrawLabel(). The transform is reset after drawing.
 *
 * @param owner The BView on which to draw (typically the BTabView).
 * @param frame The bounding rectangle of this tab.
 */
void
BTab::DrawLabel(BView* owner, BRect frame)
{
	float rotation = 0.0f;
	BPoint center(frame.left + frame.Width() / 2,
		frame.top + frame.Height() / 2);
	switch (fTabView->TabSide()) {
		case BTabView::kTopSide:
		case BTabView::kBottomSide:
			rotation = 0.0f;
			break;
		case BTabView::kLeftSide:
			rotation = 270.0f;
			break;
		case BTabView::kRightSide:
			rotation = 90.0f;
			break;
	}

	if (rotation != 0.0f) {
		// DrawLabel doesn't allow rendering rotated text
		// rotate frame first and BAffineTransform will handle the rotation
		// we can't give "unrotated" frame because it comes from
		// BTabView::TabFrame and it is also used to handle mouse clicks
		BRect originalFrame(frame);
		frame.top = center.y - originalFrame.Width() / 2;
		frame.bottom = center.y + originalFrame.Width() / 2;
		frame.left = center.x - originalFrame.Height() / 2;
		frame.right = center.x + originalFrame.Height() / 2;
	}

	BAffineTransform transform;
	transform.RotateBy(center, rotation * M_PI / 180.0f);
	owner->SetTransform(transform);

	rgb_color highColor = ui_color(B_PANEL_TEXT_COLOR);
	be_control_look->DrawLabel(owner, Label(), frame, frame,
		ui_color(B_PANEL_BACKGROUND_COLOR),
		IsEnabled() ? 0 : BControlLook::B_DISABLED,
		BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER), &highColor);
	owner->SetTransform(BAffineTransform());
}


/**
 * @brief Draws the full tab chrome (background and label).
 *
 * Delegates to BControlLook::DrawActiveTab() or DrawInactiveTab() depending
 * on whether this tab is selected, then draws the label via DrawLabel().
 *
 * @param owner  The BView on which to draw (typically the BTabView).
 * @param frame  The bounding rectangle of this tab.
 */
void
BTab::DrawTab(BView* owner, BRect frame, tab_position, bool)
{
	if (fTabView == NULL)
		return;

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	uint32 flags = 0;
	uint32 borders = _Borders(owner, frame);

	int32 index = fTabView->IndexOf(this);
	int32 selected = fTabView->Selection();
	int32 first = 0;
	int32 last = fTabView->CountTabs() - 1;

	if (index == selected) {
		be_control_look->DrawActiveTab(owner, frame, frame, base, flags,
			borders, fTabView->TabSide(), index, selected, first, last);
	} else {
		be_control_look->DrawInactiveTab(owner, frame, frame, base, flags,
			borders, fTabView->TabSide(), index, selected, first, last);
	}

	DrawLabel(owner, frame);
}


//	#pragma mark - BTab private methods


/**
 * @brief Computes the set of BControlLook border flags appropriate for this tab.
 *
 * Always includes the two borders parallel to the tab row, and conditionally
 * adds the perpendicular borders for the first and last tabs in the row.
 *
 * @param owner The BView hosting the tab row.
 * @param frame The bounding rectangle of this tab.
 * @return A bitmask of BControlLook border flags.
 */
uint32
BTab::_Borders(BView* owner, BRect frame)
{
	uint32 borders = 0;
	if (owner == NULL || fTabView == NULL)
		return borders;

	if (fTabView->TabSide() == BTabView::kTopSide
		|| fTabView->TabSide() == BTabView::kBottomSide) {
		borders = BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER;

		if (frame.left == owner->Bounds().left)
			borders |= BControlLook::B_LEFT_BORDER;

		if (frame.right == owner->Bounds().right)
			borders |= BControlLook::B_RIGHT_BORDER;
	} else if (fTabView->TabSide() == BTabView::kLeftSide
		|| fTabView->TabSide() == BTabView::kRightSide) {
		borders = BControlLook::B_LEFT_BORDER | BControlLook::B_RIGHT_BORDER;

		if (frame.top == owner->Bounds().top)
			borders |= BControlLook::B_TOP_BORDER;

		if (frame.bottom == owner->Bounds().bottom)
			borders |= BControlLook::B_BOTTOM_BORDER;
	}

	return borders;
}


//	#pragma mark - FBC padding and private methods


void BTab::_ReservedTab1() {}
void BTab::_ReservedTab2() {}
void BTab::_ReservedTab3() {}
void BTab::_ReservedTab4() {}
void BTab::_ReservedTab5() {}
void BTab::_ReservedTab6() {}
void BTab::_ReservedTab7() {}
void BTab::_ReservedTab8() {}
void BTab::_ReservedTab9() {}
void BTab::_ReservedTab10() {}
void BTab::_ReservedTab11() {}
void BTab::_ReservedTab12() {}

BTab &BTab::operator=(const BTab &)
{
	// this is private and not functional, but exported
	return *this;
}


//	#pragma mark - BTabView


/**
 * @brief Constructs a layout-enabled BTabView with the given name.
 *
 * @param name  The view name.
 * @param width Tab-width sizing policy (B_WIDTH_AS_USUAL, B_WIDTH_FROM_LABEL,
 *              or B_WIDTH_FROM_WIDEST).
 * @param flags Standard BView flags; B_SUPPORTS_LAYOUT is implied.
 */
BTabView::BTabView(const char* name, button_width width, uint32 flags)
	:
	BView(name, flags)
{
	_InitObject(true, width);
}


/**
 * @brief Constructs a frame-based (non-layout) BTabView.
 *
 * @param frame      Initial frame rectangle in the parent's coordinate system.
 * @param name       The view name.
 * @param width      Tab-width sizing policy.
 * @param resizeMask Resizing mode flags.
 * @param flags      Standard BView flags.
 */
BTabView::BTabView(BRect frame, const char* name, button_width width,
	uint32 resizeMask, uint32 flags)
	:
	BView(frame, name, resizeMask, flags)
{
	_InitObject(false, width);
}


/**
 * @brief Destructs the BTabView, deleting all BTab objects in the list.
 */
BTabView::~BTabView()
{
	for (int32 i = 0; i < CountTabs(); i++)
		delete TabAt(i);

	delete fTabList;
}


/**
 * @brief Constructs a BTabView from an archived BMessage.
 *
 * Restores tab-width policy, tab height, selection, border style, tab side,
 * and all previously archived BTab/BView pairs.
 *
 * @param archive The BMessage previously produced by BTabView::Archive().
 */
BTabView::BTabView(BMessage* archive)
	:
	BView(BUnarchiver::PrepareArchive(archive)),
	fTabList(new BList),
	fContainerView(NULL),
	fFocus(-1)
{
	BUnarchiver unarchiver(archive);

	int16 width;
	if (archive->FindInt16("_but_width", &width) == B_OK)
		fTabWidthSetting = (button_width)width;
	else
		fTabWidthSetting = B_WIDTH_AS_USUAL;

	if (archive->FindFloat("_high", &fTabHeight) != B_OK) {
		font_height fh;
		GetFontHeight(&fh);
		fTabHeight = ceilf(fh.ascent + fh.descent + fh.leading + 8.0f);
	}

	if (archive->FindInt32("_sel", &fSelection) != B_OK)
		fSelection = -1;

	if (archive->FindInt32("_border_style", (int32*)&fBorderStyle) != B_OK)
		fBorderStyle = B_FANCY_BORDER;

	if (archive->FindInt32("_TabSide", (int32*)&fTabSide) != B_OK)
		fTabSide = kTopSide;

	int32 i = 0;
	BMessage tabMsg;

	if (BUnarchiver::IsArchiveManaged(archive)) {
		int32 tabCount;
		archive->GetInfo("_l_items", NULL, &tabCount);
		for (int32 i = 0; i < tabCount; i++) {
			unarchiver.EnsureUnarchived("_l_items", i);
			unarchiver.EnsureUnarchived("_view_list", i);
		}
		return;
	}

	fContainerView = ChildAt(0);
	_InitContainerView(Flags() & B_SUPPORTS_LAYOUT);

	while (archive->FindMessage("_l_items", i, &tabMsg) == B_OK) {
		BArchivable* archivedTab = instantiate_object(&tabMsg);

		if (archivedTab) {
			BTab* tab = dynamic_cast<BTab*>(archivedTab);

			BMessage viewMsg;
			if (archive->FindMessage("_view_list", i, &viewMsg) == B_OK) {
				BArchivable* archivedView = instantiate_object(&viewMsg);
				if (archivedView)
					AddTab(dynamic_cast<BView*>(archivedView), tab);
			}
		}

		tabMsg.MakeEmpty();
		i++;
	}
}


/**
 * @brief Creates a new BTabView from an archived BMessage.
 *
 * @param archive The BMessage to instantiate from.
 * @return A new BTabView, or NULL if the archive is invalid.
 */
BArchivable*
BTabView::Instantiate(BMessage* archive)
{
	if ( validate_instantiation(archive, "BTabView"))
		return new BTabView(archive);

	return NULL;
}


/**
 * @brief Archives the BTabView's configuration and tabs into a BMessage.
 *
 * When @a deep is @c true each BTab and its associated view are also archived.
 *
 * @param archive The BMessage to archive into.
 * @param deep    If @c true, recursively archives all tabs and their views.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BTabView::Archive(BMessage* archive, bool deep) const
{
	BArchiver archiver(archive);

	status_t result = BView::Archive(archive, deep);

	if (result == B_OK)
		result = archive->AddInt16("_but_width", fTabWidthSetting);
	if (result == B_OK)
		result = archive->AddFloat("_high", fTabHeight);
	if (result == B_OK)
		result = archive->AddInt32("_sel", fSelection);
	if (result == B_OK && fBorderStyle != B_FANCY_BORDER)
		result = archive->AddInt32("_border_style", fBorderStyle);
	if (result == B_OK && fTabSide != kTopSide)
		result = archive->AddInt32("_TabSide", fTabSide);

	if (result == B_OK && deep) {
		for (int32 i = 0; i < CountTabs(); i++) {
			BTab* tab = TabAt(i);

			if ((result = archiver.AddArchivable("_l_items", tab, deep))
					!= B_OK) {
				break;
			}
			result = archiver.AddArchivable("_view_list", tab->View(), deep);
		}
	}

	return archiver.Finish(result);
}


/**
 * @brief Completes unarchiving after all child objects have been instantiated.
 *
 * Reconnects restored BTab objects to their associated BView instances and
 * selects the previously saved tab index.
 *
 * @param archive The original archive BMessage.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BTabView::AllUnarchived(const BMessage* archive)
{
	status_t err = BView::AllUnarchived(archive);
	if (err != B_OK)
		return err;

	fContainerView = ChildAt(0);
	_InitContainerView(Flags() & B_SUPPORTS_LAYOUT);

	BUnarchiver unarchiver(archive);

	int32 tabCount;
	archive->GetInfo("_l_items", NULL, &tabCount);
	for (int32 i = 0; i < tabCount && err == B_OK; i++) {
		BTab* tab;
		err = unarchiver.FindObject("_l_items", i, tab);
		if (err == B_OK && tab) {
			BView* view;
			if ((err = unarchiver.FindObject("_view_list", i,
				BUnarchiver::B_DONT_ASSUME_OWNERSHIP, view)) != B_OK)
				break;

			tab->SetView(view);
			fTabList->AddItem(tab);
		}
	}

	if (err == B_OK)
		Select(fSelection);

	return err;
}


/**
 * @brief Dispatches ABI-extension perform codes for BTabView.
 *
 * Handles PERFORM_CODE_ALL_UNARCHIVED and forwards all other codes to BView.
 *
 * @param code  The perform code identifying the virtual call to dispatch.
 * @param _data Opaque pointer to the perform-specific data structure.
 * @return B_OK on success, or an error code.
 */
status_t
BTabView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_ALL_UNARCHIVED:
		{
			perform_data_all_unarchived* data
				= (perform_data_all_unarchived*)_data;

			data->return_value = BTabView::AllUnarchived(data->archive);
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


/**
 * @brief Selects the first tab when the view is attached and no tab is selected.
 *
 * Overrides BView::AttachedToWindow() to ensure a valid initial selection.
 */
void
BTabView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (fSelection < 0 && CountTabs() > 0)
		Select(0);
}


/**
 * @brief Hook called when the view is removed from its window.
 *
 * Forwards the notification to BView::DetachedFromWindow().
 */
void
BTabView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Hook called after all children have been attached to the window.
 *
 * Forwards the notification to BView::AllAttached().
 */
void
BTabView::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Hook called after all children have been detached from the window.
 *
 * Forwards the notification to BView::AllDetached().
 */
void
BTabView::AllDetached()
{
	BView::AllDetached();
}


// #pragma mark -


/**
 * @brief Handles incoming BMessages, including scripting Get/Set for "Selection".
 *
 * Routes B_GET_PROPERTY and B_SET_PROPERTY messages that target the "Selection"
 * property to the appropriate Select() call and sends back a B_REPLY. All
 * other messages are forwarded to BView::MessageReceived().
 *
 * @param message The message to process.
 */
void
BTabView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		{
			BMessage reply(B_REPLY);
			bool handled = false;

			BMessage specifier;
			int32 index;
			int32 form;
			const char* property;
			if (message->GetCurrentSpecifier(&index, &specifier, &form,
					&property) == B_OK) {
				if (strcmp(property, "Selection") == 0) {
					if (message->what == B_GET_PROPERTY) {
						reply.AddInt32("result", fSelection);
						handled = true;
					} else {
						// B_GET_PROPERTY
						int32 selection;
						if (message->FindInt32("data", &selection) == B_OK) {
							Select(selection);
							reply.AddInt32("error", B_OK);
							handled = true;
						}
					}
				}
			}

			if (handled)
				message->SendReply(&reply);
			else
				BView::MessageReceived(message);
			break;
		}

#if 0
		// TODO this would be annoying as-is, but maybe it makes sense with
		// a modifier or using only deltaX (not the main mouse wheel)
		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaX = 0.0f;
			float deltaY = 0.0f;
			message->FindFloat("be:wheel_delta_x", &deltaX);
			message->FindFloat("be:wheel_delta_y", &deltaY);

			if (deltaX == 0.0f && deltaY == 0.0f)
				return;

			if (deltaY == 0.0f)
				deltaY = deltaX;

			int32 selection = Selection();
			int32 numTabs = CountTabs();
			if (deltaY > 0  && selection < numTabs - 1) {
				// move to the right tab.
				Select(Selection() + 1);
			} else if (deltaY < 0 && selection > 0 && numTabs > 1) {
				// move to the left tab.
				Select(selection - 1);
			}
			break;
		}
#endif

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Handles keyboard navigation between tabs.
 *
 * Arrow keys move focus to the adjacent tab; Enter/Space selects the focused
 * tab. All other keys are forwarded to BView::KeyDown().
 *
 * @param bytes    Pointer to the key bytes.
 * @param numBytes Number of bytes in @a bytes.
 */
void
BTabView::KeyDown(const char* bytes, int32 numBytes)
{
	if (IsHidden())
		return;

	switch (bytes[0]) {
		case B_DOWN_ARROW:
		case B_LEFT_ARROW: {
			int32 focus = fFocus - 1;
			if (focus < 0)
				focus = CountTabs() - 1;
			SetFocusTab(focus, true);
			break;
		}

		case B_UP_ARROW:
		case B_RIGHT_ARROW: {
			int32 focus = fFocus + 1;
			if (focus >= CountTabs())
				focus = 0;
			SetFocusTab(focus, true);
			break;
		}

		case B_RETURN:
		case B_SPACE:
			Select(FocusTab());
			break;

		default:
			BView::KeyDown(bytes, numBytes);
	}
}


/**
 * @brief Handles mouse-button presses to select tabs.
 *
 * Mouse buttons 4 and 5 (back/forward) navigate to the previous/next tab.
 * All other buttons select whichever tab the cursor is over.
 *
 * @param where The click location in the view's coordinate system.
 */
void
BTabView::MouseDown(BPoint where)
{
	// Which button is pressed?
	uint32 buttons = 0;
	BMessage* currentMessage = Window()->CurrentMessage();
	if (currentMessage != NULL) {
		currentMessage->FindInt32("buttons", (int32*)&buttons);
	}

	int32 selection = Selection();
	int32 numTabs = CountTabs();
	if (buttons & B_MOUSE_BUTTON(4)) {
		// The "back" mouse button moves to previous tab
		if (selection > 0 && numTabs > 1)
			Select(Selection() - 1);
	} else if (buttons & B_MOUSE_BUTTON(5)) {
		// The "forward" mouse button moves to next tab
		if (selection < numTabs - 1)
			Select(Selection() + 1);
	} else {
		// Other buttons are used to select a tab by clicking directly on it
		for (int32 i = 0; i < CountTabs(); i++) {
			if (TabFrame(i).Contains(where)
					&& i != Selection()) {
				Select(i);
				return;
			}
		}
	}

	BView::MouseDown(where);
}


/**
 * @brief Forwards mouse-up events to the base class.
 *
 * @param where The release location in the view's coordinate system.
 */
void
BTabView::MouseUp(BPoint where)
{
	BView::MouseUp(where);
}


/**
 * @brief Forwards mouse-moved events to the base class.
 *
 * @param where       Current cursor position in the view's coordinate system.
 * @param transit     Entry/exit transit code (B_ENTERED_VIEW, etc.).
 * @param dragMessage The dragged BMessage, or NULL if no drag is in progress.
 */
void
BTabView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	BView::MouseMoved(where, transit, dragMessage);
}


void
BTabView::Pulse()
{
	BView::Pulse();
}


/**
 * @brief Selects the tab at @a index, deselecting the previously active tab.
 *
 * Updates the BCardLayout visibility when in layout mode, scrolls the tab
 * strip to ensure the newly selected tab is visible, and shifts keyboard
 * focus to the selected tab.
 *
 * @param index Zero-based index of the tab to select. Out-of-range values
 *              keep the current selection.
 */
void
BTabView::Select(int32 index)
{
	if (index == Selection())
		return;

	if (index < 0 || index >= CountTabs())
		index = Selection();

	BTab* tab = TabAt(Selection());

	if (tab)
		tab->Deselect();

	tab = TabAt(index);
	if (tab != NULL && fContainerView != NULL) {
		if (index == 0)
			fTabOffset = 0.0f;

		tab->Select(fContainerView);
		fSelection = index;

		// make the view visible through the layout if there is one
		BCardLayout* layout
			= dynamic_cast<BCardLayout*>(fContainerView->GetLayout());
		if (layout != NULL)
			layout->SetVisibleItem(index);
	}

	Invalidate();

	if (index != 0 && !Bounds().Contains(TabFrame(index))){
		if (!Bounds().Contains(TabFrame(index).LeftTop()))
			fTabOffset += TabFrame(index).left - Bounds().left - 20.0f;
		else
			fTabOffset += TabFrame(index).right - Bounds().right + 20.0f;

		Invalidate();
	}

	SetFocusTab(index, true);
}


/**
 * @brief Returns the index of the currently selected tab.
 *
 * @return Zero-based index of the selected tab, or -1 if none is selected.
 */
int32
BTabView::Selection() const
{
	return fSelection;
}


/**
 * @brief Redraws the focus mark when the parent window's activation changes.
 *
 * @param active @c true if the window just became active.
 */
void
BTabView::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


/**
 * @brief Forwards focus changes to the currently selected tab.
 *
 * @param focus @c true to give keyboard focus to the selected tab, @c false to
 *              remove it.
 */
void
BTabView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);

	SetFocusTab(Selection(), focus);
}


/**
 * @brief Moves keyboard focus to or from a specific tab.
 *
 * When @a focus is @c true the previously focused tab loses focus and @a tab
 * gains it. When @a focus is @c false the current focus tab is cleared.
 * The affected tab areas are invalidated to update the focus mark display.
 *
 * @param tab   Zero-based index of the tab to (un)focus.
 * @param focus @c true to give focus, @c false to remove focus.
 */
void
BTabView::SetFocusTab(int32 tab, bool focus)
{
	if (tab >= CountTabs())
		tab = 0;

	if (tab < 0)
		tab = CountTabs() - 1;

	if (focus) {
		if (tab == fFocus)
			return;

		if (fFocus != -1){
			if (TabAt (fFocus) != NULL)
				TabAt(fFocus)->MakeFocus(false);
			Invalidate(TabFrame(fFocus));
		}
		if (TabAt(tab) != NULL){
			TabAt(tab)->MakeFocus(true);
			Invalidate(TabFrame(tab));
			fFocus = tab;
		}
	} else if (fFocus != -1) {
		TabAt(fFocus)->MakeFocus(false);
		Invalidate(TabFrame(fFocus));
		fFocus = -1;
	}
}


/**
 * @brief Returns the index of the tab that currently has keyboard focus.
 *
 * @return Zero-based index of the focus tab, or -1 if no tab has focus.
 */
int32
BTabView::FocusTab() const
{
	return fFocus;
}


/**
 * @brief Draws the tab strip, the content-area border box, and the focus mark.
 *
 * @param updateRect The rectangle that needs to be redrawn.
 */
void
BTabView::Draw(BRect updateRect)
{
	DrawTabs();
	DrawBox(TabFrame(fSelection));

	if (IsFocus() && fFocus != -1)
		TabAt(fFocus)->DrawFocusMark(this, TabFrame(fFocus));
}


/**
 * @brief Draws the tab-strip background and all individual tabs.
 *
 * Draws the tab-frame background first, then iterates over every tab calling
 * BTab::DrawTab().
 *
 * @return The frame rectangle of the currently selected tab, or an empty
 *         BRect if there are no tabs.
 */
BRect
BTabView::DrawTabs()
{
	BRect bounds(Bounds());
	BRect tabFrame(bounds);
	uint32 borders = 0;
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

	// set tabFrame to area around tabs
	if (fTabSide == kTopSide || fTabSide == kBottomSide) {
		if (fTabSide == kTopSide)
			tabFrame.bottom = fTabHeight;
		else
			tabFrame.top = tabFrame.bottom - fTabHeight;
	} else if (fTabSide == kLeftSide || fTabSide == kRightSide) {
		if (fTabSide == kLeftSide)
			tabFrame.right = fTabHeight;
		else
			tabFrame.left = tabFrame.right - fTabHeight;
	}

	// draw frame behind tabs
	be_control_look->DrawTabFrame(this, tabFrame, bounds, base, 0,
		borders, fBorderStyle, fTabSide);

	// draw the tabs on top of the tab frame
	int32 tabCount = CountTabs();
	for (int32 i = 0; i < tabCount; i++) {
		BRect tabFrame = TabFrame(i);

		TabAt(i)->DrawTab(this, tabFrame,
			i == fSelection ? B_TAB_FRONT
				: (i == 0) ? B_TAB_FIRST : B_TAB_ANY,
			i != fSelection - 1);
	}

	return fSelection < CountTabs() ? TabFrame(fSelection) : BRect();
}


/**
 * @brief Draws the border box that surrounds the content area.
 *
 * The border on the side where the tabs reside is omitted so the selected tab
 * visually merges with the content area. The drawing style depends on the
 * current border style (B_FANCY_BORDER, B_PLAIN_BORDER, or B_NO_BORDER).
 *
 * @param selectedTabRect The frame of the currently selected tab, used to
 *                        position the open edge of the box.
 */
void
BTabView::DrawBox(BRect selectedTabRect)
{
	BRect rect(Bounds());
	uint32 bordersToDraw = BControlLook::B_ALL_BORDERS;
	switch (fTabSide) {
		case kTopSide:
			bordersToDraw &= ~BControlLook::B_TOP_BORDER;
			rect.top = fTabHeight;
			break;
		case kBottomSide:
			bordersToDraw &= ~BControlLook::B_BOTTOM_BORDER;
			rect.bottom -= fTabHeight;
			break;
		case kLeftSide:
			bordersToDraw &= ~BControlLook::B_LEFT_BORDER;
			rect.left = fTabHeight;
			break;
		case kRightSide:
			bordersToDraw &= ~BControlLook::B_RIGHT_BORDER;
			rect.right -= fTabHeight;
			break;
	}

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (fBorderStyle == B_FANCY_BORDER)
		be_control_look->DrawGroupFrame(this, rect, rect, base, bordersToDraw);
	else if (fBorderStyle == B_PLAIN_BORDER) {
		be_control_look->DrawBorder(this, rect, rect, base, B_PLAIN_BORDER,
			0, bordersToDraw);
	} else
		; // B_NO_BORDER draws no box
}


/**
 * @brief Returns the frame rectangle of the tab at @a index.
 *
 * The frame is computed from the tab-width policy, the current tab height,
 * and the active tab side. Coordinates are in the BTabView's own space.
 *
 * @param index Zero-based index of the tab.
 * @return The tab's bounding BRect, or an empty BRect for an invalid index.
 */
BRect
BTabView::TabFrame(int32 index) const
{
	if (index >= CountTabs() || index < 0)
		return BRect();

	const float padding = ceilf(be_control_look->DefaultLabelSpacing() * 3.3f);
	const float height = fTabHeight;
	const float offset = BControlLook::ComposeSpacing(B_USE_WINDOW_SPACING);
	const BRect bounds(Bounds());

	float width = padding * 5.0f;
	switch (fTabWidthSetting) {
		case B_WIDTH_FROM_LABEL:
		{
			float x = 0.0f;
			for (int32 i = 0; i < index; i++){
				x += StringWidth(TabAt(i)->Label()) + padding;
			}

			switch (fTabSide) {
				case kTopSide:
					return BRect(offset + x, 0.0f,
						offset + x + StringWidth(TabAt(index)->Label()) + padding,
						height);
				case kBottomSide:
					return BRect(offset + x, bounds.bottom - height,
						offset + x + StringWidth(TabAt(index)->Label()) + padding,
						bounds.bottom);
				case kLeftSide:
					return BRect(0.0f, offset + x, height, offset + x
						+ StringWidth(TabAt(index)->Label()) + padding);
				case kRightSide:
					return BRect(bounds.right - height, offset + x,
						bounds.right, offset + x
							+ StringWidth(TabAt(index)->Label()) + padding);
				default:
					return BRect();
			}
		}

		case B_WIDTH_FROM_WIDEST:
			width = 0.0;
			for (int32 i = 0; i < CountTabs(); i++) {
				float tabWidth = StringWidth(TabAt(i)->Label()) + padding;
				if (tabWidth > width)
					width = tabWidth;
			}
			// fall through

		case B_WIDTH_AS_USUAL:
		default:
			switch (fTabSide) {
				case kTopSide:
					return BRect(offset + index * width, 0.0f,
						offset + index * width + width, height);
				case kBottomSide:
					return BRect(offset + index * width, bounds.bottom - height,
						offset + index * width + width, bounds.bottom);
				case kLeftSide:
					return BRect(0.0f, offset + index * width, height,
						offset + index * width + width);
				case kRightSide:
					return BRect(bounds.right - height, offset + index * width,
						bounds.right, offset + index * width + width);
				default:
					return BRect();
			}
	}
}


void
BTabView::SetFlags(uint32 flags)
{
	BView::SetFlags(flags);
}


void
BTabView::SetResizingMode(uint32 mode)
{
	BView::SetResizingMode(mode);
}


// #pragma mark -


/**
 * @brief Resizes the view to its preferred size.
 *
 * Delegates to BView::ResizeToPreferred().
 */
void
BTabView::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/**
 * @brief Returns the preferred width and height for this view.
 *
 * @param[out] _width  Receives the preferred width.
 * @param[out] _height Receives the preferred height.
 */
void
BTabView::GetPreferredSize(float* _width, float* _height)
{
	BView::GetPreferredSize(_width, _height);
}


/**
 * @brief Returns the minimum size needed to display at least two tabs.
 *
 * Combines the minimum tab-strip size with the container view's minimum size
 * plus border widths.
 *
 * @return The minimum BSize; honors any explicit minimum set by the caller.
 */
BSize
BTabView::MinSize()
{
	BSize size;
	if (GetLayout())
		size = GetLayout()->MinSize();
	else {
		size = _TabsMinSize();
		BSize containerSize = fContainerView->MinSize();
		containerSize.width += 2 * _BorderWidth();
		containerSize.height += 2 * _BorderWidth();
		if (containerSize.width > size.width)
			size.width = containerSize.width;
		size.height += containerSize.height;
	}
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Returns the maximum size of the tab view.
 *
 * Combines the minimum tab-strip size with the container view's maximum size
 * plus border widths.
 *
 * @return The maximum BSize; honors any explicit maximum set by the caller.
 */
BSize
BTabView::MaxSize()
{
	BSize size;
	if (GetLayout())
		size = GetLayout()->MaxSize();
	else {
		size = _TabsMinSize();
		BSize containerSize = fContainerView->MaxSize();
		containerSize.width += 2 * _BorderWidth();
		containerSize.height += 2 * _BorderWidth();
		if (containerSize.width > size.width)
			size.width = containerSize.width;
		size.height += containerSize.height;
	}
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


/**
 * @brief Returns the preferred size of the tab view.
 *
 * Combines the minimum tab-strip size with the container view's preferred size
 * plus border widths.
 *
 * @return The preferred BSize; honors any explicit preferred size set by
 *         the caller.
 */
BSize
BTabView::PreferredSize()
{
	BSize size;
	if (GetLayout() != NULL)
		size = GetLayout()->PreferredSize();
	else {
		size = _TabsMinSize();
		BSize containerSize = fContainerView->PreferredSize();
		containerSize.width += 2 * _BorderWidth();
		containerSize.height += 2 * _BorderWidth();
		if (containerSize.width > size.width)
			size.width = containerSize.width;
		size.height += containerSize.height;
	}
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


/**
 * @brief Called when the view's frame position changes.
 *
 * @param newPosition The new top-left corner in the parent's coordinate system.
 */
void
BTabView::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Called when the view's frame dimensions change.
 *
 * @param newWidth  The new width of the view.
 * @param newHeight The new height of the view.
 */
void
BTabView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
}


// #pragma mark -


/**
 * @brief Resolves a scripting specifier to the appropriate handler.
 *
 * Handles the "Selection" property itself; delegates everything else to
 * BView::ResolveSpecifier().
 *
 * @param message   The scripting message.
 * @param index     Index into the message's specifier stack.
 * @param specifier The current specifier BMessage.
 * @param what      The specifier type constant.
 * @param property  The property name string.
 * @return @c this if the property is "Selection", otherwise the result from
 *         BView::ResolveSpecifier().
 */
BHandler*
BTabView::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo propInfo(sPropertyList);

	if (propInfo.FindMatch(message, 0, specifier, what, property) >= B_OK)
		return this;

	return BView::ResolveSpecifier(message, index, specifier, what, property);
}


/**
 * @brief Reports the scripting suites supported by BTabView.
 *
 * Adds "suite/vnd.Be-tab-view" and the property info table before delegating
 * to BView::GetSupportedSuites().
 *
 * @param message The BMessage to populate with suite information.
 * @return B_OK on success, or an error code.
 */
status_t
BTabView::GetSupportedSuites(BMessage* message)
{
	message->AddString("suites", "suite/vnd.Be-tab-view");

	BPropertyInfo propInfo(sPropertyList);
	message->AddFlat("messages", &propInfo);

	return BView::GetSupportedSuites(message);
}


// #pragma mark -


/**
 * @brief Adds a tab to the tab view, optionally using a caller-supplied BTab.
 *
 * If @a tab is NULL a new BTab is created. The target view is registered with
 * the container's BCardLayout (if present) and, when this is the first tab and
 * the view is already attached, it is selected immediately.
 *
 * @param target The content view for the new tab.
 * @param tab    An existing BTab to use, or NULL to create a default one.
 */
void
BTabView::AddTab(BView* target, BTab* tab)
{
	if (tab == NULL)
		tab = new BTab(target);
	else
		tab->SetView(target);

	if (fContainerView->GetLayout())
		fContainerView->GetLayout()->AddView(CountTabs(), target);

	fTabList->AddItem(tab);
	BTab::Private(tab).SetTabView(this);

	// When we haven't had a any tabs before, but are already attached to the
	// window, select this one.
	if (CountTabs() == 1 && Window() != NULL)
		Select(0);
}


/**
 * @brief Removes and returns the tab at @a index.
 *
 * The removed tab is deselected and detached from the tab view. Adjusts the
 * selection and focus indices so that a valid tab remains selected.
 *
 * @param index Zero-based index of the tab to remove.
 * @return The removed BTab (caller takes ownership), or NULL on failure.
 */
BTab*
BTabView::RemoveTab(int32 index)
{
	if (index < 0 || index >= CountTabs())
		return NULL;

	BTab* tab = (BTab*)fTabList->RemoveItem(index);
	if (tab == NULL)
		return NULL;

	tab->Deselect();
	BTab::Private(tab).SetTabView(NULL);

	if (fContainerView->GetLayout())
		fContainerView->GetLayout()->RemoveItem(index);

	if (CountTabs() == 0)
		fFocus = -1;
	else if (index <= fSelection)
		Select(fSelection - 1);

	if (fFocus >= 0) {
		if (fFocus == CountTabs() - 1 || CountTabs() == 0)
			SetFocusTab(fFocus, false);
		else
			SetFocusTab(fFocus, true);
	}

	return tab;
}


/**
 * @brief Returns the BTab at @a index.
 *
 * @param index Zero-based index into the tab list.
 * @return The BTab at @a index, or NULL if @a index is out of range.
 */
BTab*
BTabView::TabAt(int32 index) const
{
	return (BTab*)fTabList->ItemAt(index);
}


/**
 * @brief Sets the tab-width sizing policy and invalidates the view.
 *
 * @param width The sizing policy: B_WIDTH_AS_USUAL, B_WIDTH_FROM_LABEL, or
 *              B_WIDTH_FROM_WIDEST.
 */
void
BTabView::SetTabWidth(button_width width)
{
	fTabWidthSetting = width;

	Invalidate();
}


/**
 * @brief Returns the current tab-width sizing policy.
 *
 * @return The active button_width policy.
 */
button_width
BTabView::TabWidth() const
{
	return fTabWidthSetting;
}


/**
 * @brief Sets the height of the tab strip and repositions the container view.
 *
 * Has no effect if @a height equals the current tab height.
 *
 * @param height The new tab-strip height in pixels.
 */
void
BTabView::SetTabHeight(float height)
{
	if (fTabHeight == height)
		return;

	fTabHeight = height;
	_LayoutContainerView(GetLayout() != NULL);

	Invalidate();
}


/**
 * @brief Returns the current tab-strip height in pixels.
 *
 * @return The tab height.
 */
float
BTabView::TabHeight() const
{
	return fTabHeight;
}


/**
 * @brief Sets the border style of the content-area box.
 *
 * @param borderStyle B_FANCY_BORDER, B_PLAIN_BORDER, or B_NO_BORDER.
 */
void
BTabView::SetBorder(border_style borderStyle)
{
	if (fBorderStyle == borderStyle)
		return;

	fBorderStyle = borderStyle;

	_LayoutContainerView((Flags() & B_SUPPORTS_LAYOUT) != 0);
}


/**
 * @brief Returns the current content-area border style.
 *
 * @return The border_style constant in use.
 */
border_style
BTabView::Border() const
{
	return fBorderStyle;
}


/**
 * @brief Sets which side of the view the tab strip appears on.
 *
 * @param tabSide kTopSide, kBottomSide, kLeftSide, or kRightSide.
 */
void
BTabView::SetTabSide(tab_side tabSide)
{
	if (fTabSide == tabSide)
		return;

	fTabSide = tabSide;
	_LayoutContainerView(Flags() & B_SUPPORTS_LAYOUT);
}


/**
 * @brief Returns the side of the view on which the tab strip is drawn.
 *
 * @return The active tab_side value.
 */
BTabView::tab_side
BTabView::TabSide() const
{
	return fTabSide;
}


/**
 * @brief Returns the internal container view that hosts tab content.
 *
 * @return The container BView managed by this BTabView.
 */
BView*
BTabView::ContainerView() const
{
	return fContainerView;
}


/**
 * @brief Returns the total number of tabs in the view.
 *
 * @return The tab count.
 */
int32
BTabView::CountTabs() const
{
	return fTabList->CountItems();
}


/**
 * @brief Returns the content view associated with the tab at @a tabIndex.
 *
 * Convenience wrapper around TabAt() and BTab::View().
 *
 * @param tabIndex Zero-based index of the tab.
 * @return The BView for that tab, or NULL if the index is invalid or the tab
 *         has no associated view.
 */
BView*
BTabView::ViewForTab(int32 tabIndex) const
{
	BTab* tab = TabAt(tabIndex);
	if (tab != NULL)
		return tab->View();

	return NULL;
}


/**
 * @brief Returns the index of @a tab within the tab list.
 *
 * @param tab The BTab to look up.
 * @return The zero-based index, or -1 if @a tab is not found.
 */
int32
BTabView::IndexOf(BTab* tab) const
{
	if (tab != NULL) {
		int32 tabCount = CountTabs();
		for (int32 index = 0; index < tabCount; index++) {
			if (TabAt(index) == tab)
				return index;
		}
	}

	return -1;
}


/**
 * @brief Performs common initialization shared by all BTabView constructors.
 *
 * Sets up the tab list, selection state, appearance defaults, and calls
 * _InitContainerView().
 *
 * @param layouted If @c true, creates a layout-aware container using
 *                 BGroupLayout / BCardLayout.
 * @param width    Initial tab-width sizing policy.
 */
void
BTabView::_InitObject(bool layouted, button_width width)
{
	fTabList = new BList;

	fTabWidthSetting = width;
	fSelection = -1;
	fFocus = -1;
	fTabOffset = 0.0f;
	fBorderStyle = B_FANCY_BORDER;
	fTabSide = kTopSide;

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);

	font_height fh;
	GetFontHeight(&fh);
	fTabHeight = ceilf(fh.ascent + fh.descent + fh.leading +
		(be_control_look->DefaultLabelSpacing() * 1.3f));

	fContainerView = NULL;
	_InitContainerView(layouted);
}


/**
 * @brief Creates and positions the container view if it does not yet exist.
 *
 * In layout mode a BGroupLayout is installed on the tab view and the container
 * uses a BCardLayout. In non-layout mode the container spans the full bounds
 * minus the tab-strip height.
 *
 * @param layouted @c true to create a layout-managed container.
 */
void
BTabView::_InitContainerView(bool layouted)
{
	bool needsLayout = false;
	bool createdContainer = false;
	if (layouted) {
		if (GetLayout() == NULL) {
			SetLayout(new(std::nothrow) BGroupLayout(B_HORIZONTAL));
			needsLayout = true;
		}

		if (fContainerView == NULL) {
			fContainerView = new BView("view container", B_WILL_DRAW);
			fContainerView->SetLayout(new(std::nothrow) BCardLayout());
			createdContainer = true;
		}
	} else if (fContainerView == NULL) {
		fContainerView = new BView(Bounds(), "view container", B_FOLLOW_ALL,
			B_WILL_DRAW);
		createdContainer = true;
	}

	if (needsLayout || createdContainer)
		_LayoutContainerView(layouted);

	if (createdContainer) {
		fContainerView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		fContainerView->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
		AddChild(fContainerView);
	}
}


/**
 * @brief Returns the minimum size required to display the first two tabs.
 *
 * Used internally by MinSize(), MaxSize(), and PreferredSize() to account for
 * the tab-strip portion of the overall minimum size.
 *
 * @return The minimum BSize for the tab strip.
 */
BSize
BTabView::_TabsMinSize() const
{
	BSize size(0.0f, TabHeight());
	int32 count = min_c(2, CountTabs());
	for (int32 i = 0; i < count; i++) {
		BRect frame = TabFrame(i);
		size.width += frame.Width();
	}

	if (count < CountTabs()) {
		// TODO: Add size for yet to be implemented buttons that allow
		// "scrolling" the displayed tabs left/right.
	}

	return size;
}


/**
 * @brief Returns the pixel width of the content-area border for the current style.
 *
 * @return 3 for B_FANCY_BORDER, 1 for B_PLAIN_BORDER, 0 for B_NO_BORDER.
 */
float
BTabView::_BorderWidth() const
{
	switch (fBorderStyle) {
		default:
		case B_FANCY_BORDER:
			return 3.0f;

		case B_PLAIN_BORDER:
			return 1.0f;

		case B_NO_BORDER:
			return 0.0f;
	}
}


/**
 * @brief Updates the container view's position and size after a layout change.
 *
 * In layout mode, adjusts the BGroupLayout insets so the tab strip and border
 * are reserved on the correct side. In non-layout mode, repositions and resizes
 * the container directly.
 *
 * @param layouted @c true when the tab view is operating in layout mode.
 */
void
BTabView::_LayoutContainerView(bool layouted)
{
	float borderWidth = _BorderWidth();
	if (layouted) {
		float topBorderOffset;
		switch (fBorderStyle) {
			default:
			case B_FANCY_BORDER:
				topBorderOffset = 1.0f;
				break;

			case B_PLAIN_BORDER:
				topBorderOffset = 0.0f;
				break;

			case B_NO_BORDER:
				topBorderOffset = -1.0f;
				break;
		}
		BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
		if (layout != NULL) {
			float inset = borderWidth + TabHeight() - topBorderOffset;
			switch (fTabSide) {
				case kTopSide:
					layout->SetInsets(borderWidth, inset, borderWidth,
						borderWidth);
					break;
				case kBottomSide:
					layout->SetInsets(borderWidth, borderWidth, borderWidth,
						inset);
					break;
				case kLeftSide:
					layout->SetInsets(inset, borderWidth, borderWidth,
						borderWidth);
					break;
				case kRightSide:
					layout->SetInsets(borderWidth, borderWidth, inset,
						borderWidth);
					break;
			}
		}
	} else {
		BRect bounds = Bounds();
		switch (fTabSide) {
			case kTopSide:
				bounds.top += TabHeight();
				break;
			case kBottomSide:
				bounds.bottom -= TabHeight();
				break;
			case kLeftSide:
				bounds.left += TabHeight();
				break;
			case kRightSide:
				bounds.right -= TabHeight();
				break;
		}
		bounds.InsetBy(borderWidth, borderWidth);

		fContainerView->MoveTo(bounds.left, bounds.top);
		fContainerView->ResizeTo(bounds.Width(), bounds.Height());
	}
}


// #pragma mark - FBC and forbidden


void BTabView::_ReservedTabView3() {}
void BTabView::_ReservedTabView4() {}
void BTabView::_ReservedTabView5() {}
void BTabView::_ReservedTabView6() {}
void BTabView::_ReservedTabView7() {}
void BTabView::_ReservedTabView8() {}
void BTabView::_ReservedTabView9() {}
void BTabView::_ReservedTabView10() {}
void BTabView::_ReservedTabView11() {}
void BTabView::_ReservedTabView12() {}


BTabView::BTabView(const BTabView& tabView)
	: BView(tabView)
{
	// this is private and not functional, but exported
}


BTabView&
BTabView::operator=(const BTabView&)
{
	// this is private and not functional, but exported
	return *this;
}

//	#pragma mark - binary compatibility


extern "C" void
B_IF_GCC_2(_ReservedTabView1__8BTabView, _ZN8BTabView17_ReservedTabView1Ev)(
	BTabView* tabView, border_style borderStyle)
{
	tabView->BTabView::SetBorder(borderStyle);
}

extern "C" void
B_IF_GCC_2(_ReservedTabView2__8BTabView, _ZN8BTabView17_ReservedTabView2Ev)(
	BTabView* tabView, BTabView::tab_side tabSide)
{
	tabView->BTabView::SetTabSide(tabSide);
}
