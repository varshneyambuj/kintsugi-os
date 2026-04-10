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
 *       Marc Flerackers, mflerackers@androme.be
 *       Stefano Ceccherini, stefano.ceccherini@gmail.com
 */


/**
 * @file MenuWindow.cpp
 * @brief Implementation of BMenuWindow, the internal pop-up window used by BMenu
 *
 * BMenuWindow is a lightweight, borderless window that hosts a BMenu's content
 * when it opens as a pop-up. It manages scroll arrows for menus that exceed
 * the screen height.
 *
 * @see BMenu, BWindow
 */


#include <MenuWindow.h>

#include <ControlLook.h>
#include <Debug.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Screen.h>

#include <MenuPrivate.h>
#include <WindowPrivate.h>


namespace BPrivate {

/**
 * @brief Base class for the up- and down-scroll arrow views used by BMenuWindow.
 *
 * Maintains an enabled/disabled state. Subclasses draw the specific arrow
 * direction when enabled, and a dimmed arrow when disabled.
 */
class BMenuScroller : public BView {
public:
							BMenuScroller(BRect frame);

			bool			IsEnabled() const;
			void			SetEnabled(bool enabled);

private:
			bool			fEnabled;
};


/**
 * @brief Internal view that frames and hosts the BMenu inside BMenuWindow.
 *
 * Adds the BMenu as a child when attached, removes it when detached, and draws
 * an "empty" label when the menu has no items.
 */
class BMenuFrame : public BView {
public:
							BMenuFrame(BMenu* menu);

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			Draw(BRect updateRect);
	virtual	void			LayoutChanged();

			void			MoveSubmenusOver(BMenu* menu,
								BRect menuFrame,
								BRect screenFrame);

private:
	friend class BMenuWindow;

			BMenu*			fMenu;
};


/**
 * @brief Scroll arrow view drawn at the top of an overflowing menu.
 */
class UpperScroller : public BMenuScroller {
public:
							UpperScroller(BRect frame);

	virtual	void			Draw(BRect updateRect);
};


/**
 * @brief Scroll arrow view drawn at the bottom of an overflowing menu.
 */
class LowerScroller : public BMenuScroller {
public:
							LowerScroller(BRect frame);

	virtual	void			Draw(BRect updateRect);
};


}	// namespace BPrivate


using namespace BPrivate;


/** @brief Fixed height in pixels of each scroll arrow band. */
const int kScrollerHeight = 12;


/**
 * @brief Constructs a BMenuScroller view with a disabled initial state.
 *
 * @param frame The bounding rectangle for this scroll arrow view.
 */
BMenuScroller::BMenuScroller(BRect frame)
	:
	BView(frame, "menu scroller", 0, B_WILL_DRAW | B_FRAME_EVENTS
		| B_FULL_UPDATE_ON_RESIZE),
	fEnabled(false)
{
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


/**
 * @brief Returns whether the scroll arrow is currently enabled.
 *
 * A disabled scroller draws a dimmed arrow and does not respond to cursor
 * hover in CheckForScrolling().
 *
 * @return true if the scroller is enabled.
 */
bool
BMenuScroller::IsEnabled() const
{
	return fEnabled;
}


/**
 * @brief Enables or disables the scroll arrow.
 *
 * Does not automatically invalidate the view; the caller is responsible for
 * calling Invalidate() after changing the state.
 *
 * @param enabled true to enable scrolling, false to disable it.
 */
void
BMenuScroller::SetEnabled(bool enabled)
{
	fEnabled = enabled;
}


//	#pragma mark -


/**
 * @brief Constructs the upper scroll arrow view.
 *
 * @param frame The bounding rectangle for this view, typically at the top
 *              of the BMenuWindow.
 */
UpperScroller::UpperScroller(BRect frame)
	:
	BMenuScroller(frame)
{
}


/**
 * @brief Draws the upward-pointing triangle scroll arrow.
 *
 * Fills the background with a slightly darkened menu color, then draws a
 * filled upward triangle centered horizontally. The triangle is black when
 * enabled and dimmed to B_DARKEN_2_TINT when disabled.
 *
 * @param updateRect The region that needs to be redrawn.
 */
void
UpperScroller::Draw(BRect updateRect)
{
	SetLowColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR), B_DARKEN_1_TINT));
	float middle = Bounds().right / 2;

	// Draw the upper arrow.
	if (IsEnabled())
		SetHighColor(0, 0, 0);
	else {
		SetHighColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
	}

	FillRect(Bounds(), B_SOLID_LOW);

	FillTriangle(BPoint(middle, (kScrollerHeight / 2) - 3),
		BPoint(middle + 5, (kScrollerHeight / 2) + 2),
		BPoint(middle - 5, (kScrollerHeight / 2) + 2));
}


//	#pragma mark -


/**
 * @brief Constructs the lower scroll arrow view.
 *
 * @param frame The bounding rectangle for this view, typically at the bottom
 *              of the BMenuWindow.
 */
LowerScroller::LowerScroller(BRect frame)
	:
	BMenuScroller(frame)
{
}


/**
 * @brief Draws the downward-pointing triangle scroll arrow.
 *
 * Fills the background with a slightly darkened menu color, then draws a
 * filled downward triangle centered horizontally. The triangle is black when
 * enabled and dimmed to B_DARKEN_2_TINT when disabled.
 *
 * @param updateRect The region that needs to be redrawn.
 */
void
LowerScroller::Draw(BRect updateRect)
{
	SetLowColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR), B_DARKEN_1_TINT));

	BRect frame = Bounds();
	// Draw the lower arrow.
	if (IsEnabled())
		SetHighColor(0, 0, 0);
	else {
		SetHighColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
	}

	FillRect(frame, B_SOLID_LOW);

	float middle = Bounds().right / 2;

	FillTriangle(BPoint(middle, frame.bottom - (kScrollerHeight / 2) + 3),
		BPoint(middle + 5, frame.bottom - (kScrollerHeight / 2) - 2),
		BPoint(middle - 5, frame.bottom - (kScrollerHeight / 2) - 2));
}


//	#pragma mark -


/**
 * @brief Constructs a BMenuFrame that will host @a menu.
 *
 * @param menu The BMenu to display; it will be added as a child when this
 *             view is attached to a window.
 */
BMenuFrame::BMenuFrame(BMenu *menu)
	:
	BView(BRect(0, 0, 1, 1), "menu frame", B_FOLLOW_ALL_SIDES, B_WILL_DRAW),
	fMenu(menu)
{
}


/**
 * @brief Adds the hosted BMenu as a child and matches the frame to the window size.
 *
 * Also copies the menu's font onto this view so that font metrics used for the
 * empty-menu label are consistent with the menu's own metrics.
 */
void
BMenuFrame::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (fMenu != NULL)
		AddChild(fMenu);

	ResizeTo(Window()->Bounds().Width(), Window()->Bounds().Height());
	if (fMenu != NULL) {
		BFont font;
		fMenu->GetFont(&font);
		SetFont(&font);
	}
}


/**
 * @brief Removes the hosted BMenu from this view's child list when detached.
 */
void
BMenuFrame::DetachedFromWindow()
{
	if (fMenu != NULL)
		RemoveChild(fMenu);
}


/**
 * @brief Draws the frame background and, for empty menus, a placeholder label.
 *
 * When the menu contains no items the frame draws the standard menu background
 * and a centered, dimmed "empty" string so the user sees a visible (but
 * non-interactive) pop-up rather than a zero-size window.
 *
 * @param updateRect The region that needs to be redrawn.
 */
void
BMenuFrame::Draw(BRect updateRect)
{
	if (fMenu != NULL && fMenu->CountItems() == 0) {
		BRect rect(Bounds());
		be_control_look->DrawMenuBackground(this, rect, updateRect,
			ui_color(B_MENU_BACKGROUND_COLOR));
		SetDrawingMode(B_OP_OVER);

		// TODO: Review this as it's a bit hacky.
		// Since there are no items in this menu, its size is 0x0.
		// To show an empty BMenu, we use BMenuFrame to draw an empty item.
		// It would be nice to simply add a real "empty" item, but in that case
		// we couldn't tell if the item was added by us or not, and applications
		// could break (because CountItems() would return 1 for an empty BMenu).
		// See also BMenu::UpdateWindowViewSize()
		font_height height;
		GetFontHeight(&height);
		SetHighColor(tint_color(ui_color(B_MENU_ITEM_TEXT_COLOR),
			ui_color(B_MENU_ITEM_TEXT_COLOR).IsDark() ? B_LIGHTEN_1_TINT : B_DISABLED_LABEL_TINT));
		BPoint where(
			(Bounds().Width() - fMenu->StringWidth(kEmptyMenuLabel)) / 2,
			ceilf(height.ascent + 1));
		DrawString(kEmptyMenuLabel, where);
	}
}


/**
 * @brief Responds to layout changes by repositioning open sub-menus.
 *
 * Recursively walks open submenus and shifts their windows so they remain
 * on-screen after the parent menu's layout changes (e.g., after the window
 * is moved to a different monitor position).
 */
void
BMenuFrame::LayoutChanged()
{
	if (fMenu == NULL || Window() == NULL)
		return BView::LayoutChanged();

	// shift child menus over recursively
	MoveSubmenusOver(fMenu, fMenu->ConvertToScreen(fMenu->Frame()),
		(BScreen(fMenu->Window())).Frame());

	BView::LayoutChanged();
}


/**
 * @brief Recursively repositions open submenus to stay within screen bounds.
 *
 * Starting from @a menu, finds any open submenu and computes a new position
 * that keeps it adjacent to its parent item and within @a screenFrame. The
 * submenu is placed to the right of the parent item when possible, or to the
 * left if the right side is off-screen. Vertical clamping is applied to keep
 * the submenu on-screen.
 *
 * @param menu        The menu whose open submenus should be repositioned.
 * @param menuFrame   The current screen-coordinate frame of @a menu.
 * @param screenFrame The frame of the screen to clamp to.
 */
void
BMenuFrame::MoveSubmenusOver(BMenu* menu, BRect menuFrame, BRect screenFrame)
{
	if (menu == NULL)
		return;

	BMenu* submenu;
	BMenuWindow* submenuWindow;
	BPoint submenuLoc;
	BRect submenuFrame;

	int32 itemCount = menu->CountItems();
	for (int32 index = 0; index < itemCount; index++) {
		submenu = menu->SubmenuAt(index);
		if (submenu == NULL || submenu->Window() == NULL)
			continue; // not an open submenu, next

		submenuWindow = dynamic_cast<BMenuWindow*>(submenu->Window());
		if (submenuWindow == NULL)
			break; // submenu window was not a BMenuWindow, strange if true

		// found an open submenu, get submenu frame
		if (submenu->LockLooper()) {
			// need to lock looper because we're in a different thread
			submenuFrame = submenu->Frame();
			submenu->ConvertToScreen(&submenuFrame);
			submenu->UnlockLooper();
		} else {
			// give up
			break;
		}

		// get submenu loc and convert it to screen coords using menu
		if (menu->LockLooper()) {
			// check if submenu should be displayed right or left of menu
			BRect superFrame = submenu->Superitem()->Frame();
			if (submenuFrame.right < menuFrame.right)
				submenuLoc = superFrame.LeftTop() - BPoint(submenuFrame.Width() + 1, -1);
			else
				submenuLoc = superFrame.RightTop() + BPoint(1, 1);

			menu->ConvertToScreen(&submenuLoc);
			submenuFrame.OffsetTo(submenuLoc);
			menu->UnlockLooper();
		} else {
			// give up
			break;
		}

		// move submenu frame into screen bounds vertically
		if (submenuFrame.Height() < screenFrame.Height()) {
			if (submenuFrame.bottom >= screenFrame.bottom)
				submenuLoc.y -= (submenuFrame.bottom - screenFrame.bottom);
			else if (submenuFrame.top <= screenFrame.top)
				submenuLoc.y += (screenFrame.top - submenuFrame.top);
		} else {
			// put menu at top of screen, turn on the scroll arrows
			submenuLoc.y = 0;
		}

		// move submenu window into place
		submenuWindow->MoveTo(submenuLoc);

		// recurse through submenu's submenus
		MoveSubmenusOver(submenu, submenuFrame, screenFrame);

		// we're done with this menu
		break;
	}
}


//	#pragma mark -


/**
 * @brief Constructs a BMenuWindow with no menu attached.
 *
 * Creates a bordered window with menu-appropriate window feel and flags.
 * Size limits are set to a practical minimum so that BMenu can resize the
 * window to fit its content.
 *
 * @param name The internal window name used for identification.
 *
 * @see AttachMenu(), DetachMenu()
 */
BMenuWindow::BMenuWindow(const char *name)
	// The window will be resized by BMenu, so just pass a dummy rect
	:
	BWindow(BRect(0, 0, 0, 0), name, B_BORDERED_WINDOW_LOOK, kMenuWindowFeel,
		B_NOT_MOVABLE | B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AVOID_FOCUS
			| kAcceptKeyboardFocusFlag),
	fMenu(NULL),
	fMenuFrame(NULL),
	fUpperScroller(NULL),
	fLowerScroller(NULL),
	fScrollStep(19)
{
	SetSizeLimits(2, 10000, 2, 10000);
}


/**
 * @brief Destroys the BMenuWindow, detaching any hosted menu and scrollers.
 */
BMenuWindow::~BMenuWindow()
{
	DetachMenu();
}


/**
 * @brief Dispatches a message by delegating to BWindow::DispatchMessage().
 *
 * @param message The message to dispatch.
 * @param handler The target handler.
 */
void
BMenuWindow::DispatchMessage(BMessage *message, BHandler *handler)
{
	BWindow::DispatchMessage(message, handler);
}


/**
 * @brief Attaches @a menu to this window, making it the hosted menu.
 *
 * Creates a BMenuFrame for @a menu, adds it as a child, and gives the menu
 * keyboard focus. Calling this method when a menu is already attached triggers
 * a debugger() call.
 *
 * @param menu The BMenu to attach; may be NULL (no-op).
 *
 * @see DetachMenu(), AttachScrollers()
 */
void
BMenuWindow::AttachMenu(BMenu *menu)
{
	if (fMenuFrame)
		debugger("BMenuWindow: a menu is already attached!");
	if (menu != NULL) {
		fMenuFrame = new BMenuFrame(menu);
		AddChild(fMenuFrame);
		menu->MakeFocus(true);
		fMenu = menu;
	}
}


/**
 * @brief Detaches the hosted menu from this window and frees the frame view.
 *
 * Calls DetachScrollers() first, then removes and deletes the BMenuFrame.
 * Safe to call when no menu is attached.
 *
 * @see AttachMenu(), DetachScrollers()
 */
void
BMenuWindow::DetachMenu()
{
	DetachScrollers();
	if (fMenuFrame) {
		RemoveChild(fMenuFrame);
		delete fMenuFrame;
		fMenuFrame = NULL;
		fMenu = NULL;
	}
}


/**
 * @brief Creates and attaches scroll arrow views when the menu overflows the window.
 *
 * Adds UpperScroller and LowerScroller views to the window and shrinks the
 * BMenuFrame to make room. Enables the arrows depending on the current scroll
 * position relative to the scroll limit. If scrollers already exist they are
 * resized rather than recreated.
 *
 * @see DetachScrollers(), TryScrollBy(), TryScrollTo()
 */
void
BMenuWindow::AttachScrollers()
{
	// We want to attach a scroller only if there's a
	// menu frame already existing.
	if (!fMenu || !fMenuFrame)
		return;

	fMenu->MakeFocus(true);

	BRect frame = Bounds();
	float newLimit = fMenu->Bounds().Height()
		- (frame.Height() - 2 * kScrollerHeight);

	if (!HasScrollers())
		fValue = 0;
	else if (fValue > newLimit)
		_ScrollBy(newLimit - fValue);

	fLimit = newLimit;

	if (fUpperScroller == NULL) {
		fUpperScroller = new UpperScroller(
			BRect(0, 0, frame.right, kScrollerHeight - 1));
		AddChild(fUpperScroller);
	}

	if (fLowerScroller == NULL) {
		fLowerScroller = new LowerScroller(
			BRect(0, frame.bottom - kScrollerHeight + 1, frame.right,
				frame.bottom));
		AddChild(fLowerScroller);
	}

	fUpperScroller->ResizeTo(frame.right, kScrollerHeight - 1);
	fLowerScroller->ResizeTo(frame.right, kScrollerHeight - 1);

	fUpperScroller->SetEnabled(fValue > 0);
	fLowerScroller->SetEnabled(fValue < fLimit);

	fMenuFrame->ResizeTo(frame.Width(), frame.Height() - 2 * kScrollerHeight);
	fMenuFrame->MoveTo(0, kScrollerHeight);
}


/**
 * @brief Removes and deletes the scroll arrow views and restores the frame size.
 *
 * Scrolls the menu back to the top (position 0, 0), then removes and frees
 * both the upper and lower scroll views. The BMenuFrame is resized back to
 * fill the full window bounds.
 *
 * @see AttachScrollers()
 */
void
BMenuWindow::DetachScrollers()
{
	// BeOS doesn't remember the position where the last scrolling ended,
	// so we just scroll back to the beginning.
	if (fMenu)
		fMenu->ScrollTo(0, 0);

	if (fLowerScroller) {
		RemoveChild(fLowerScroller);
		delete fLowerScroller;
		fLowerScroller = NULL;
	}

	if (fUpperScroller) {
		RemoveChild(fUpperScroller);
		delete fUpperScroller;
		fUpperScroller = NULL;
	}

	BRect frame = Bounds();

	if (fMenuFrame != NULL) {
		fMenuFrame->ResizeTo(frame.Width(), frame.Height());
		fMenuFrame->MoveTo(0, 0);
	}
}


/**
 * @brief Sets the per-tick scroll step used by the scroll arrows.
 *
 * @param step The distance in pixels to scroll each time the arrow is active.
 *
 * @see GetSteps()
 */
void
BMenuWindow::SetSmallStep(float step)
{
	fScrollStep = step;
}


/**
 * @brief Returns the small and large scroll step sizes.
 *
 * The small step is the per-tick value set by SetSmallStep(). The large step
 * is the visible frame height minus the small step, or twice the small step
 * if no menu frame is attached.
 *
 * @param _smallStep Receives the small (per-tick) step; may be NULL.
 * @param _largeStep Receives the large (page) step; may be NULL.
 *
 * @see SetSmallStep(), TryScrollBy()
 */
void
BMenuWindow::GetSteps(float* _smallStep, float* _largeStep) const
{
	if (_smallStep != NULL)
		*_smallStep = fScrollStep;
	if (_largeStep != NULL) {
		if (fMenuFrame != NULL)
			*_largeStep = fMenuFrame->Bounds().Height() - fScrollStep;
		else
			*_largeStep = fScrollStep * 2;
	}
}


/**
 * @brief Returns whether scroll arrow views are currently attached.
 *
 * @return true if both the upper and lower scrollers are present and the menu
 *         frame is attached.
 */
bool
BMenuWindow::HasScrollers() const
{
	return fMenuFrame != NULL && fUpperScroller != NULL
		&& fLowerScroller != NULL;
}


/**
 * @brief Scrolls the menu if @a cursor is over an active scroll arrow.
 *
 * Checks whether the screen-coordinate point @a cursor falls within an enabled
 * upper or lower scroller, and if so calls _ScrollBy() with ±smallStep. A
 * brief snooze is inserted to throttle the scroll rate.
 *
 * @param cursor The current cursor position in screen coordinates.
 * @return true if a scroll was performed; false if the cursor is not over an
 *         active arrow or scrollers are not attached.
 *
 * @see TryScrollBy(), HasScrollers()
 */
bool
BMenuWindow::CheckForScrolling(const BPoint &cursor)
{
	if (!fMenuFrame || !fUpperScroller || !fLowerScroller)
		return false;

	return _Scroll(cursor);
}


/**
 * @brief Scrolls the menu by @a step pixels if scrollers are attached.
 *
 * A positive @a step scrolls downward; a negative value scrolls upward.
 * The scroll is clamped to the valid range [0, fLimit].
 *
 * @param step The number of pixels to scroll; may be negative.
 * @return true if scrollers are attached and the scroll was applied;
 *         false otherwise.
 *
 * @see TryScrollTo(), CheckForScrolling()
 */
bool
BMenuWindow::TryScrollBy(const float& step)
{
	if (!fMenuFrame || !fUpperScroller || !fLowerScroller)
		return false;

	_ScrollBy(step);
	return true;
}


/**
 * @brief Scrolls the menu to an absolute vertical position.
 *
 * Computes the delta between @a where and the current scroll position and
 * delegates to _ScrollBy().
 *
 * @param where The target vertical scroll position in pixels.
 * @return true if scrollers are attached and the scroll was applied;
 *         false otherwise.
 *
 * @see TryScrollBy()
 */
bool
BMenuWindow::TryScrollTo(const float& where)
{
	if (!fMenuFrame || !fUpperScroller || !fLowerScroller)
		return false;

	_ScrollBy(where - fValue);
	return true;
}


/**
 * @brief Checks whether @a where is over an active scroller and scrolls if so.
 *
 * Converts @a where from screen to window coordinates, then tests against the
 * lower and upper scroller frames. If the cursor is inside an enabled scroller
 * the menu is scrolled by ±smallStep and the function returns true after a
 * brief snooze.
 *
 * @param where The cursor position in screen coordinates.
 * @return true if scrolling occurred; false if no active scroller was hit.
 */
bool
BMenuWindow::_Scroll(const BPoint& where)
{
	ASSERT((fLowerScroller != NULL));
	ASSERT((fUpperScroller != NULL));

	const BPoint cursor = ConvertFromScreen(where);
	const BRect &lowerFrame = fLowerScroller->Frame();
	const BRect &upperFrame = fUpperScroller->Frame();

	int32 delta = 0;
	if (fLowerScroller->IsEnabled() && lowerFrame.Contains(cursor))
		delta = 1;
	else if (fUpperScroller->IsEnabled() && upperFrame.Contains(cursor))
		delta = -1;

	if (delta == 0)
		return false;

	float smallStep;
	GetSteps(&smallStep, NULL);
	_ScrollBy(smallStep * delta);

	snooze(5000);

	return true;
}


/**
 * @brief Scrolls the hosted menu by @a step pixels and updates arrow states.
 *
 * Clamps the scroll so that fValue stays within [0, fLimit]. Enables or
 * disables each arrow view as the boundary is approached or reached, and
 * invalidates the affected arrow view to trigger a redraw.
 *
 * @param step The pixel amount to scroll; positive scrolls downward, negative
 *             scrolls upward.
 */
void
BMenuWindow::_ScrollBy(const float& step)
{
	if (step > 0) {
		if (fValue == 0) {
			fUpperScroller->SetEnabled(true);
			fUpperScroller->Invalidate();
		}

		if (fValue + step >= fLimit) {
			// If we reached the limit, only scroll to the end
			fMenu->ScrollBy(0, fLimit - fValue);
			fValue = fLimit;
			fLowerScroller->SetEnabled(false);
			fLowerScroller->Invalidate();
		} else {
			fMenu->ScrollBy(0, step);
			fValue += step;
		}
	} else if (step < 0) {
		if (fValue == fLimit) {
			fLowerScroller->SetEnabled(true);
			fLowerScroller->Invalidate();
		}

		if (fValue + step <= 0) {
			fMenu->ScrollBy(0, -fValue);
			fValue = 0;
			fUpperScroller->SetEnabled(false);
			fUpperScroller->Invalidate();
		} else {
			fMenu->ScrollBy(0, step);
			fValue += step;
		}
	}
}
