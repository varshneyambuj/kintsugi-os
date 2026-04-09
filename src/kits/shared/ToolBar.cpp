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
 *   Copyright 2011 Stephan Aßmus <superstippi@gmx.de>
 *   All rights reserved. Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file ToolBar.cpp
 *  @brief Implements BToolBar, a BGroupView-based toolbar widget that manages
 *         icon buttons, separators, and glue items, along with the helper
 *         classes ToolBarButton and LockableButton.
 */

#include "ToolBar.h"

#include <Button.h>
#include <ControlLook.h>
#include <Message.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>


namespace BPrivate {



//! Button to adopt background color of BToolBar
class ToolBarButton : public BButton {
public:
			ToolBarButton(const char* name, const char* label,
				BMessage* message);

	void	AttachedToWindow();
};


/** @brief Constructs a ToolBarButton.
 *  @param name     Internal view name.
 *  @param label    Text label (may be NULL for icon-only buttons).
 *  @param message  The message sent when the button is clicked.
 */
ToolBarButton::ToolBarButton(const char* name, const char* label,
				BMessage* message)
	:
	BButton(name, label, message)
{
}


/** @brief Called when the button is attached to a window.
 *
 *  Delegates to BButton::AttachedToWindow.  Color adoption from the
 *  toolbar parent may be implemented here in the future.
 */
void
ToolBarButton::AttachedToWindow()
{
	BButton::AttachedToWindow();

	// TODO: Should we force Control, Menu, or parent colors here?
}


// # pragma mark -


class LockableButton : public ToolBarButton {
public:
			LockableButton(const char* name, const char* label,
				BMessage* message);

	void	MouseDown(BPoint point);
};


/** @brief Constructs a LockableButton.
 *  @param name     Internal view name.
 *  @param label    Text label (may be NULL).
 *  @param message  The message sent when the button is clicked.
 */
LockableButton::LockableButton(const char* name, const char* label,
	BMessage* message)
	:
	ToolBarButton(name, label, message)
{
}


/** @brief Handles mouse-down, switching between toggle and normal behavior.
 *
 *  When the Shift key is held or the button is already pressed,
 *  toggle behavior is used; otherwise normal push-button behavior applies.
 *  The chosen behavior is recorded in the message's "behavior" field.
 *
 *  @param point  The position of the click (view coordinates).
 */
void
LockableButton::MouseDown(BPoint point)
{
	if ((modifiers() & B_SHIFT_KEY) != 0 || Value() == B_CONTROL_ON)
		SetBehavior(B_TOGGLE_BEHAVIOR);
	else
		SetBehavior(B_BUTTON_BEHAVIOR);

	Message()->SetInt32("behavior", Behavior());
	ToolBarButton::MouseDown(point);
}


//#pragma mark  -


/** @brief Constructs a BToolBar with an explicit frame rectangle.
 *  @param frame  Initial position and size of the toolbar.
 *  @param ont    Orientation of the toolbar (B_HORIZONTAL or B_VERTICAL).
 */
BToolBar::BToolBar(BRect frame, orientation ont)
	:
	BGroupView(ont),
	fOrientation(ont)
{
	_Init();

	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
	SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
}


/** @brief Constructs a BToolBar without a fixed frame (layout-driven).
 *  @param ont  Orientation of the toolbar (B_HORIZONTAL or B_VERTICAL).
 */
BToolBar::BToolBar(orientation ont)
	:
	BGroupView(ont),
	fOrientation(ont)
{
	_Init();
}


/** @brief Destructor. */
BToolBar::~BToolBar()
{
}


/** @brief Hides the toolbar and suppresses all child tool tips.
 *
 *  Overrides BView::Hide() to also call _HideToolTips() because hidden
 *  child buttons are not technically hidden themselves from their
 *  perspective.
 */
void
BToolBar::Hide()
{
	BView::Hide();
	// TODO: This could be fixed in BView instead. Looking from the
	// BButtons, they are not hidden though, only their parent is...
	_HideToolTips();
}


/** @brief Adds an action button identified by a command constant.
 *  @param command      The command value used as the message 'what' field.
 *  @param target       The handler that receives the button's message.
 *  @param icon         The bitmap icon to display on the button.
 *  @param toolTipText  Optional tooltip string; NULL for no tooltip.
 *  @param text         Optional text label; NULL for icon-only.
 *  @param lockable     If true, the button can be locked in a pressed state.
 */
void
BToolBar::AddAction(uint32 command, BHandler* target, const BBitmap* icon,
	const char* toolTipText, const char* text, bool lockable)
{
	AddAction(new BMessage(command), target, icon, toolTipText, text, lockable);
}


/** @brief Adds an action button with a pre-built BMessage.
 *  @param message      The message sent when the button is clicked (adopted).
 *  @param target       The handler that receives the button's message.
 *  @param icon         The bitmap icon to display on the button.
 *  @param toolTipText  Optional tooltip string; NULL for no tooltip.
 *  @param text         Optional text label; NULL for icon-only.
 *  @param lockable     If true, the button supports toggle (lock) behavior.
 */
void
BToolBar::AddAction(BMessage* message, BHandler* target,
	const BBitmap* icon, const char* toolTipText, const char* text,
	bool lockable)
{
	ToolBarButton* button;
	if (lockable)
		button = new LockableButton(NULL, NULL, message);
	else
		button = new ToolBarButton(NULL, NULL, message);
	button->SetIcon(icon);
	button->SetFlat(true);
	if (toolTipText != NULL)
		button->SetToolTip(toolTipText);
	if (text != NULL)
		button->SetLabel(text);
	AddView(button);
	button->SetTarget(target);
}


/** @brief Appends a visual separator between groups of actions.
 *
 *  The separator orientation is perpendicular to the toolbar orientation.
 */
void
BToolBar::AddSeparator()
{
	orientation ont = (fOrientation == B_HORIZONTAL) ?
		B_VERTICAL : B_HORIZONTAL;
	AddView(new BSeparatorView(ont, B_PLAIN_BORDER));
}


/** @brief Appends an expanding glue item that pushes subsequent items to the end. */
void
BToolBar::AddGlue()
{
	GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
}


/** @brief Appends an arbitrary view to the toolbar's group layout.
 *  @param view  The view to add.
 */
void
BToolBar::AddView(BView* view)
{
	GroupLayout()->AddView(view);
}


/** @brief Enables or disables the button associated with \a command.
 *  @param command  The command constant that identifies the button.
 *  @param enabled  True to enable the button; false to disable it.
 */
void
BToolBar::SetActionEnabled(uint32 command, bool enabled)
{
	if (BButton* button = FindButton(command))
		button->SetEnabled(enabled);
}


/** @brief Sets the pressed/active state of the button associated with \a command.
 *  @param command  The command constant that identifies the button.
 *  @param pressed  True to show the button as pressed; false for normal.
 */
void
BToolBar::SetActionPressed(uint32 command, bool pressed)
{
	if (BButton* button = FindButton(command))
		button->SetValue(pressed);
}


/** @brief Shows or hides the layout item wrapping the button for \a command.
 *  @param command  The command constant that identifies the button.
 *  @param visible  True to show the button; false to hide it.
 */
void
BToolBar::SetActionVisible(uint32 command, bool visible)
{
	BButton* button = FindButton(command);
	if (button == NULL)
		return;
	for (int32 i = 0; BLayoutItem* item = GroupLayout()->ItemAt(i); i++) {
		if (item->View() != button)
			continue;
		item->SetVisible(visible);
		break;
	}
}


/** @brief Searches for the first button whose message 'what' matches \a command.
 *  @param command  The command constant to search for.
 *  @return Pointer to the matching BButton, or NULL if not found.
 */
BButton*
BToolBar::FindButton(uint32 command) const
{
	for (int32 i = 0; BView* view = ChildAt(i); i++) {
		BButton* button = dynamic_cast<BButton*>(view);
		if (button == NULL)
			continue;
		BMessage* message = button->Message();
		if (message == NULL)
			continue;
		if (message->what == command) {
			return button;
			// Assumes there is only one button with this message...
			break;
		}
	}
	return NULL;
}


// #pragma mark - Private methods


/** @brief Pulse handler — hides tool tips when the toolbar becomes hidden. */
void
BToolBar::Pulse()
{
	// TODO: Perhaps this could/should be addressed in BView instead.
	if (IsHidden())
		_HideToolTips();
}


/** @brief Handles resize events by invalidating to fix app_server update issues.
 *  @param width   New width in pixels.
 *  @param height  New height in pixels.
 */
void
BToolBar::FrameResized(float width, float height)
{
	// TODO: There seems to be a bug in app_server which does not
	// correctly trigger invalidation of views which are shown, when
	// the resulting dirty area is somehow already part of an update region.
	Invalidate();
}


/** @brief Initialises layout, insets, colors, and required view flags. */
void
BToolBar::_Init()
{
	float inset = ceilf(be_control_look->DefaultItemSpacing() / 2);
	GroupLayout()->SetInsets(inset, 0, inset, 0);
	GroupLayout()->SetSpacing(1);

	SetFlags(Flags() | B_FRAME_EVENTS | B_PULSE_NEEDED);

	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


/** @brief Hides the tool tip of every direct child view. */
void
BToolBar::_HideToolTips() const
{
	for (int32 i = 0; BView* view = ChildAt(i); i++)
		view->HideToolTip();
}


} // namespace BPrivate
