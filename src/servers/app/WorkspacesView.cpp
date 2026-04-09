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
 *   Copyright 2005-2009, Haiku Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 */

/** @file WorkspacesView.cpp
 *  @brief Server-side view that renders a miniature overview of all workspaces
 *         and handles mouse-driven window and workspace switching.
 */

#include "WorkspacesView.h"

#include "AppServer.h"
#include "Decorator.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "InterfaceDefs.h"
#include "ServerApp.h"
#include "Window.h"
#include "Workspace.h"

#include <WindowPrivate.h>


/** @brief Constructs a WorkspacesView.
 *
 *  Delegates to the View base constructor and initialises tracking variables
 *  to their idle state.  Sets the low colour to white and the high colour to
 *  black for the initial draw state.
 *
 *  @param frame          The view's frame rectangle.
 *  @param scrollingOffset The initial scroll offset applied to the view bounds.
 *  @param name           The view name.
 *  @param token          The server-side token that identifies this view.
 *  @param resizeMode     The resize mode flags controlling layout behaviour.
 *  @param flags          Additional view flags.
 */
WorkspacesView::WorkspacesView(BRect frame, BPoint scrollingOffset,
		const char* name, int32 token, uint32 resizeMode, uint32 flags)
	:
	View(frame, scrollingOffset, name, token, resizeMode, flags),
	fSelectedWindow(NULL),
	fSelectedWorkspace(-1),
	fHasMoved(false)
{
	fDrawState->SetLowColor((rgb_color){ 255, 255, 255, 255 });
	fDrawState->SetHighColor((rgb_color){ 0, 0, 0, 255 });
}


/** @brief Destructor. */
WorkspacesView::~WorkspacesView()
{
}


/** @brief Called when this view is attached to a Window.
 *
 *  Registers the view with both the window (incrementing its workspaces-view
 *  count) and the desktop's global workspaces-view list so that the view
 *  receives update notifications when workspace or window state changes.
 *
 *  @param window The Window this view is being attached to.
 */
void
WorkspacesView::AttachedToWindow(::Window* window)
{
	View::AttachedToWindow(window);

	window->AddWorkspacesView();
	window->Desktop()->AddWorkspacesView(this);
}


/** @brief Called when this view is detached from its Window.
 *
 *  Unregisters the view from the desktop's workspaces-view list and decrements
 *  the owning window's workspaces-view count before delegating to the base class.
 */
void
WorkspacesView::DetachedFromWindow()
{
	fWindow->Desktop()->RemoveWorkspacesView(this);
	fWindow->RemoveWorkspacesView();

	View::DetachedFromWindow();
}


/** @brief Retrieves the current workspace grid dimensions.
 *
 *  Reads the number of workspace columns and rows from the desktop settings
 *  and writes them into the output parameters.
 *
 *  @param columns Output parameter set to the number of workspace columns.
 *  @param rows    Output parameter set to the number of workspace rows.
 */
void
WorkspacesView::_GetGrid(int32& columns, int32& rows)
{
	DesktopSettings settings(Window()->Desktop());

	columns = settings.WorkspacesColumns();
	rows = settings.WorkspacesRows();
}


/*!	\brief Returns the frame of the screen for the specified workspace.
*/
/** @brief Returns the physical screen frame for the workspace at index \a i.
 *  @param i Zero-based workspace index.
 *  @return The screen frame in screen coordinates for that workspace.
 */
BRect
WorkspacesView::_ScreenFrame(int32 i)
{
	return Window()->Desktop()->WorkspaceFrame(i);
}


/*!	\brief Returns the frame of the specified workspace within the
		workspaces view.
*/
/** @brief Computes the view-local rectangle occupied by workspace \a i.
 *
 *  Divides the view's bounds evenly among all workspace cells and returns the
 *  cell rectangle for workspace \a i.  The rightmost column and bottom row are
 *  extended to the view edge to eliminate rounding gaps.
 *
 *  @param i Zero-based workspace index.
 *  @return The workspace cell rectangle in screen coordinates.
 */
BRect
WorkspacesView::_WorkspaceAt(int32 i)
{
	int32 columns, rows;
	_GetGrid(columns, rows);

	BRect frame = Bounds();
	LocalToScreenTransform().Apply(&frame);

	int32 width = frame.IntegerWidth() / columns;
	int32 height = frame.IntegerHeight() / rows;

	int32 column = i % columns;
	int32 row = i / columns;

	BRect rect(column * width, row * height, (column + 1) * width,
		(row + 1) * height);

	rect.OffsetBy(frame.LeftTop());

	// make sure there is no gap anywhere
	if (column == columns - 1)
		rect.right = frame.right;
	if (row == rows - 1)
		rect.bottom = frame.bottom;

	return rect;
}


/*!	\brief Returns the workspace frame and index of the workspace
		under \a where.

	If, for some reason, there is no workspace located under \where,
	an empty rectangle is returned, and \a index is set to -1.
*/
/** @brief Finds the workspace cell that contains the screen point \a where.
 *
 *  Iterates over all workspace cells in reverse order (front to back) and
 *  returns the frame and index of the first cell that contains \a where.
 *
 *  @param where  The point to test in screen coordinates.
 *  @param index  Output parameter set to the workspace index, or -1 if none.
 *  @return The workspace cell rectangle, or an empty BRect if \a where is
 *          outside all cells.
 */
BRect
WorkspacesView::_WorkspaceAt(BPoint where, int32& index)
{
	int32 columns, rows;
	_GetGrid(columns, rows);

	for (index = columns * rows; index-- > 0;) {
		BRect workspaceFrame = _WorkspaceAt(index);

		if (workspaceFrame.Contains(where))
			return workspaceFrame;
	}

	return BRect();
}


/** @brief Scales and positions a window frame into the corresponding workspace cell.
 *
 *  Applies uniform scaling from \a screenFrame to \a workspaceFrame space,
 *  offsets to the cell origin, and snaps coordinates to integer pixels.
 *
 *  @param workspaceFrame The destination cell rectangle in screen coordinates.
 *  @param screenFrame    The full physical screen rectangle.
 *  @param windowFrame    The window frame in screen coordinates.
 *  @param windowPosition The actual top-left position of the window (may differ
 *                        from windowFrame.LeftTop() for non-current workspaces).
 *  @return The scaled and offset window frame within the workspace cell.
 */
BRect
WorkspacesView::_WindowFrame(const BRect& workspaceFrame,
	const BRect& screenFrame, const BRect& windowFrame,
	BPoint windowPosition)
{
	BRect frame = windowFrame;
	frame.OffsetTo(windowPosition);

	// scale down the rect
	float factor = workspaceFrame.Width() / screenFrame.Width();
	frame.left = frame.left * factor;
	frame.right = frame.right * factor;

	factor = workspaceFrame.Height() / screenFrame.Height();
	frame.top = frame.top * factor;
	frame.bottom = frame.bottom * factor;

	// offset by the workspace fame position
	// and snap to integer coordinates without distorting the size too much
	frame.OffsetTo(rintf(frame.left + workspaceFrame.left),
		rintf(frame.top + workspaceFrame.top));
	frame.right = rintf(frame.right);
	frame.bottom = rintf(frame.bottom);

	return frame;
}


/** @brief Draws a single window's miniature representation into a workspace cell.
 *
 *  Skips desktop-feel and hidden windows.  Draws the title tab (if any),
 *  the window border, the white fill, and a truncated title string.  Colours
 *  are dimmed for inactive workspaces.  The drawn area is subtracted from
 *  \a backgroundRegion so subsequent windows can avoid overdraw.
 *
 *  @param drawingEngine      The engine used for all drawing operations.
 *  @param workspaceFrame     The workspace cell rectangle in screen coordinates.
 *  @param screenFrame        The full physical screen rectangle for the workspace.
 *  @param window             The Window to render.
 *  @param windowPosition     The window's top-left anchor position for this workspace.
 *  @param backgroundRegion   Region tracking undrawn background area; updated in place.
 *  @param workspaceActive    True if \a workspace is the currently active workspace.
 */
void
WorkspacesView::_DrawWindow(DrawingEngine* drawingEngine,
	const BRect& workspaceFrame, const BRect& screenFrame, ::Window* window,
	BPoint windowPosition, BRegion& backgroundRegion, bool workspaceActive)
{
	if (window->Feel() == kDesktopWindowFeel || window->IsHidden())
		return;

	BPoint offset = window->Frame().LeftTop() - windowPosition;
	BRect frame = _WindowFrame(workspaceFrame, screenFrame, window->Frame(),
		windowPosition);
	Decorator *decorator = window->Decorator();
	BRect tabFrame(0, 0, 0, 0);
	if (decorator != NULL)
		tabFrame = decorator->TitleBarRect();

	tabFrame = _WindowFrame(workspaceFrame, screenFrame,
		tabFrame, tabFrame.LeftTop() - offset);
	if (!workspaceFrame.Intersects(frame)
		&& !workspaceFrame.Intersects(tabFrame))
		return;

	rgb_color activeTabColor = (rgb_color){ 255, 203, 0, 255 };
	rgb_color inactiveTabColor = (rgb_color){ 232, 232, 232, 255 };
	rgb_color navColor = (rgb_color){ 0, 0, 229, 255 };
	rgb_color activeFrameColor = (rgb_color){ 180, 180, 180, 255 };
	rgb_color inactiveFrameColor = (rgb_color){ 180, 180, 180, 255 };
	if (decorator != NULL) {
		activeTabColor = decorator->UIColor(B_WINDOW_TAB_COLOR);
		inactiveTabColor = decorator->UIColor(B_WINDOW_INACTIVE_TAB_COLOR);
		navColor = decorator->UIColor(B_NAVIGATION_BASE_COLOR);
		activeFrameColor = decorator->UIColor(B_WINDOW_BORDER_COLOR);
		inactiveFrameColor = decorator->UIColor(B_WINDOW_INACTIVE_BORDER_COLOR);
	}

	rgb_color white = (rgb_color){ 255, 255, 255, 255 };

	rgb_color tabColor = inactiveTabColor;
	rgb_color frameColor = inactiveFrameColor;
	if (window->IsFocus()) {
		tabColor = activeTabColor;
		frameColor = activeFrameColor;
	}

	if (!workspaceActive) {
		_DarkenColor(tabColor);
		_DarkenColor(frameColor);
		_DarkenColor(white);
	}

	if (tabFrame.Height() > 0 && tabFrame.Width() > 0) {
		float width = tabFrame.Width();
		if (tabFrame.left < frame.left) {
			// Shift the tab right
			tabFrame.left = frame.left;
			tabFrame.right = tabFrame.left + width;
		}
		if (tabFrame.right > frame.right) {
			// Shift the tab left
			tabFrame.right = frame.right;
			tabFrame.left = tabFrame.right - width;
		}

		if (tabFrame.bottom >= tabFrame.top) {
			// Shift the tab up
			float tabHeight = tabFrame.Height();
			tabFrame.bottom = frame.top - 1;
			tabFrame.top = tabFrame.bottom - tabHeight;
		}

		tabFrame = tabFrame & workspaceFrame;

		if (decorator != NULL && tabFrame.IsValid()) {
			drawingEngine->FillRect(tabFrame, tabColor);
			backgroundRegion.Exclude(tabFrame);
		}
	}

	drawingEngine->StrokeRect(frame, frameColor);

	BRect fillFrame = frame.InsetByCopy(1, 1) & workspaceFrame;
	frame = frame & workspaceFrame;
	if (!frame.IsValid())
		return;

	// fill the window itself
	drawingEngine->FillRect(fillFrame, white);

	// draw title

	// TODO: the mini-window functionality should probably be moved into the
	// Window class, so that it has only to be recalculated on demand. With
	// double buffered windows, this would also open up the door to have a
	// more detailed preview.
	BString title(window->Title());

	const ServerFont& font = fDrawState->Font();

	font.TruncateString(&title, B_TRUNCATE_END, fillFrame.Width() - 4);
	font_height fontHeight;
	font.GetHeight(fontHeight);
	float height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
	if (title.Length() > 0 && height < frame.Height() - 2) {
		rgb_color textColor = tint_color(white, B_DARKEN_4_TINT);
		drawingEngine->SetHighColor(textColor);
		drawingEngine->SetLowColor(white);

		float width = font.StringWidth(title.String(), title.Length());

		BPoint textOffset;
		textOffset.x = rintf(frame.left + (frame.Width() - width) / 2);
		textOffset.y = rintf(frame.top + (frame.Height() - height) / 2
			+ fontHeight.ascent);
		drawingEngine->DrawString(title.String(), title.Length(), textOffset);
	}

	// prevent the next window down from drawing over this window
	backgroundRegion.Exclude(frame);
}


/** @brief Draws a complete workspace cell including its background and all windows.
 *
 *  Draws the active/selected border, then iterates over the workspace's
 *  windows from front to back using _DrawWindow() (which removes each drawn
 *  area from \a redraw).  Finally fills the remaining clipped area with the
 *  workspace background colour.
 *
 *  @param drawingEngine The engine used for all drawing operations.
 *  @param redraw        The region that needs to be repainted; used as the
 *                       clipping region throughout.
 *  @param index         Zero-based workspace index to draw.
 */
void
WorkspacesView::_DrawWorkspace(DrawingEngine* drawingEngine,
	BRegion& redraw, int32 index)
{
	BRect rect = _WorkspaceAt(index);

	Workspace workspace(*Window()->Desktop(), index, true);
	bool workspaceActive = workspace.IsCurrent();
	if (workspaceActive) {
		// draw active frame
		rgb_color black = (rgb_color){ 0, 0, 0, 255 };
		drawingEngine->StrokeRect(rect, black);
	} else if (index == fSelectedWorkspace) {
		rgb_color gray = (rgb_color){ 80, 80, 80, 255 };
		drawingEngine->StrokeRect(rect, gray);
	}

	rect.InsetBy(1, 1);

	rgb_color color = workspace.Color();
	if (!workspaceActive)
		_DarkenColor(color);

	// draw windows

	BRegion backgroundRegion = redraw;

	// TODO: would be nice to get the real update region here

	BRect screenFrame = _ScreenFrame(index);

	BRegion workspaceRegion(rect);
	backgroundRegion.IntersectWith(&workspaceRegion);
	drawingEngine->ConstrainClippingRegion(&backgroundRegion);

	ServerFont font = fDrawState->Font();
	font.SetSize(fWindow->ServerWindow()->App()->PlainFont().Size());
	float reducedSize = ceilf(max_c(8.0f,
		min_c(Frame().Height(), Frame().Width()) / 15));
	if (font.Size() > reducedSize)
		font.SetSize(reducedSize);
	fDrawState->SetFont(font);
	drawingEngine->SetFont(font);

	// We draw from top down and cut the window out of the clipping region
	// which reduces the flickering
	::Window* window;
	BPoint leftTop;
	while (workspace.GetPreviousWindow(window, leftTop) == B_OK) {
		_DrawWindow(drawingEngine, rect, screenFrame, window,
			leftTop, backgroundRegion, workspaceActive);
	}

	// draw background
	drawingEngine->FillRect(rect, color);

	drawingEngine->ConstrainClippingRegion(&redraw);
}


/** @brief Applies a darkening tint to \a color in place.
 *  @param color The colour to darken; modified in place.
 */
void
WorkspacesView::_DarkenColor(rgb_color& color) const
{
	color = tint_color(color, B_DARKEN_2_TINT);
}


/** @brief Marks this view's entire screen-space bounds as dirty to trigger a repaint. */
void
WorkspacesView::_Invalidate() const
{
	BRect frame = Bounds();
	LocalToScreenTransform().Apply(&frame);

	BRegion region(frame), expose;
	Window()->MarkContentDirty(region, expose);
}


/** @brief Draws the full workspaces overview.
 *
 *  Renders the grid lines separating workspace cells (excluding the active
 *  workspace cell to reduce flicker), then draws each workspace cell from
 *  back to front using _DrawWorkspace().  Restores the server window's draw
 *  state after rendering.
 *
 *  @param drawingEngine          The engine used for all drawing.
 *  @param effectiveClipping      The effective clipping region for this draw pass.
 *  @param windowContentClipping  The window content clipping region.
 *  @param deep                   Unused by this override; reserved for child views.
 */
void
WorkspacesView::Draw(DrawingEngine* drawingEngine, const BRegion* effectiveClipping,
	const BRegion* windowContentClipping, bool deep)
{
	// we can only draw within our own area
	BRegion redraw(ScreenAndUserClipping(windowContentClipping));
	// add the current clipping
	redraw.IntersectWith(effectiveClipping);

	int32 columns, rows;
	_GetGrid(columns, rows);

	// draw grid

	// make sure the grid around the active workspace is not drawn
	// to reduce flicker
	BRect activeRect = _WorkspaceAt(Window()->Desktop()->CurrentWorkspace());
	BRegion gridRegion(redraw);
	gridRegion.Exclude(activeRect);
	drawingEngine->ConstrainClippingRegion(&gridRegion);

	BRect frame = Bounds();
	LocalToScreenTransform().Apply(&frame);

	// horizontal lines

	drawingEngine->StrokeLine(BPoint(frame.left, frame.top),
		BPoint(frame.right, frame.top), ViewColor());

	for (int32 row = 0; row < rows; row++) {
		BRect rect = _WorkspaceAt(row * columns);
		drawingEngine->StrokeLine(BPoint(frame.left, rect.bottom),
			BPoint(frame.right, rect.bottom), ViewColor());
	}

	// vertical lines

	drawingEngine->StrokeLine(BPoint(frame.left, frame.top),
		BPoint(frame.left, frame.bottom), ViewColor());

	for (int32 column = 0; column < columns; column++) {
		BRect rect = _WorkspaceAt(column);
		drawingEngine->StrokeLine(BPoint(rect.right, frame.top),
			BPoint(rect.right, frame.bottom), ViewColor());
	}

	drawingEngine->ConstrainClippingRegion(&redraw);

	// draw workspaces

	for (int32 i = rows * columns; i-- > 0;) {
		_DrawWorkspace(drawingEngine, redraw, i);
	}
	fWindow->ServerWindow()->ResyncDrawState();
}


/** @brief Handles mouse-button-down events for workspace and window selection.
 *
 *  Resets tracking state, then (if the primary button is pressed) determines
 *  which workspace and window were clicked.  Applies modifier-key shortcuts:
 *  Control activates or minimises the clicked window; Option sends it behind.
 *  Without modifiers, the window is selected for dragging if it is movable.
 *
 *  @param message The mouse-down BMessage containing button and modifier info.
 *  @param where   The click position in screen coordinates.
 */
void
WorkspacesView::MouseDown(BMessage* message, BPoint where)
{
	// reset tracking variables
	fSelectedWorkspace = -1;
	fSelectedWindow = NULL;
	fHasMoved = false;

	// check if the correct mouse button is pressed
	int32 buttons;
	if (message->FindInt32("buttons", &buttons) != B_OK
		|| (buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
		return;

	int32 index;
	BRect workspaceFrame = _WorkspaceAt(where, index);
	if (index < 0)
		return;

	Workspace workspace(*Window()->Desktop(), index);
	workspaceFrame.InsetBy(1, 1);

	BRect screenFrame = _ScreenFrame(index);

	::Window* window;
	BRect windowFrame;
	BPoint leftTop;
	while (workspace.GetPreviousWindow(window, leftTop) == B_OK) {
		if (window->IsMinimized() || window->IsHidden())
			continue;

		BRect frame = _WindowFrame(workspaceFrame, screenFrame, window->Frame(),
			leftTop);
		if (frame.Contains(where) && window->Feel() != kDesktopWindowFeel
			&& window->Feel() != kWindowScreenFeel) {
			fSelectedWindow = window;
			windowFrame = frame;
			break;
		}
	}

	// Some special functionality (clicked with modifiers)

	int32 modifiers;
	if (fSelectedWindow != NULL
		&& message->FindInt32("modifiers", &modifiers) == B_OK) {
		if ((modifiers & B_CONTROL_KEY) != 0) {
			// Activate window if clicked with the control key pressed,
			// minimize it if control+shift - this mirrors Deskbar
			// shortcuts (when pressing a team menu item).
			if ((modifiers & B_SHIFT_KEY) != 0)
				fSelectedWindow->ServerWindow()->NotifyMinimize(true);
			else
				Window()->Desktop()->ActivateWindow(fSelectedWindow);
			fSelectedWindow = NULL;
		} else if ((modifiers & B_OPTION_KEY) != 0) {
			// Also, send window to back if clicked with the option
			// key pressed.
			Window()->Desktop()->SendWindowBehind(fSelectedWindow);
			fSelectedWindow = NULL;
		}
	}

	// If this window is movable, we keep it selected
	// (we prevent our own window from being moved, too)

	if ((fSelectedWindow != NULL
			&& (fSelectedWindow->Flags() & B_NOT_MOVABLE) != 0)
		|| fSelectedWindow == Window()) {
		fSelectedWindow = NULL;
	}

	fLeftTopOffset = where - windowFrame.LeftTop();
	fSelectedWorkspace = index;
	fClickPoint = where;

	if (index >= 0)
		_Invalidate();
}


/** @brief Handles mouse-button-up events.
 *
 *  If no drag occurred and a workspace was selected, switches to that workspace.
 *  Clears the selection highlight by invalidating the view if a window was
 *  selected.  Resets all tracking state.
 *
 *  @param message The mouse-up BMessage (unused beyond signature compatibility).
 *  @param where   The release position in screen coordinates.
 */
void
WorkspacesView::MouseUp(BMessage* message, BPoint where)
{
	if (!fHasMoved && fSelectedWorkspace >= 0) {
		int32 index;
		_WorkspaceAt(where, index);
		if (index >= 0)
			Window()->Desktop()->SetWorkspaceAsync(index);
	}

	if (fSelectedWindow != NULL) {
		// We need to hide the selection frame again
		_Invalidate();
	}

	fSelectedWindow = NULL;
	fSelectedWorkspace = -1;
}


/** @brief Handles mouse-movement events for dragging windows between workspaces.
 *
 *  If a window is being dragged and has moved more than 2 pixels from the
 *  click point, it is repositioned (and if necessary moved to a new workspace)
 *  to track the mouse.  If only a workspace is selected (no window), the
 *  selection highlight follows the pointer.
 *
 *  @param message The mouse-moved BMessage containing the current button state.
 *  @param where   The current pointer position in screen coordinates.
 */
void
WorkspacesView::MouseMoved(BMessage* message, BPoint where)
{
	if (fSelectedWindow == NULL && fSelectedWorkspace < 0)
		return;

	// check if the correct mouse button is pressed
	int32 buttons;
	if (message->FindInt32("buttons", &buttons) != B_OK
		|| (buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
		return;

	if (!fHasMoved) {
		Window()->Desktop()->SetMouseEventWindow(Window());
			// don't let us off the mouse
	}

	int32 index;
	BRect workspaceFrame = _WorkspaceAt(where, index);

	if (fSelectedWindow == NULL) {
		if (fSelectedWorkspace >= 0 && fSelectedWorkspace != index) {
			fSelectedWorkspace = index;
			_Invalidate();
		}
		return;
	}

	workspaceFrame.InsetBy(1, 1);

	if (index != fSelectedWorkspace) {
		if (fSelectedWindow->IsNormal() && !fSelectedWindow->InWorkspace(index)) {
			// move window to this new workspace
			uint32 newWorkspaces = (fSelectedWindow->Workspaces()
				& ~(1UL << fSelectedWorkspace)) | (1UL << index);

			Window()->Desktop()->SetWindowWorkspaces(fSelectedWindow,
				newWorkspaces);
		}
		fSelectedWorkspace = index;
	}

	BRect screenFrame = _ScreenFrame(index);
	float left = rintf((where.x - workspaceFrame.left - fLeftTopOffset.x)
		* screenFrame.Width() / workspaceFrame.Width());
	float top = rintf((where.y - workspaceFrame.top - fLeftTopOffset.y)
		* screenFrame.Height() / workspaceFrame.Height());

	BPoint leftTop;
	if (fSelectedWorkspace == Window()->Desktop()->CurrentWorkspace())
		leftTop = fSelectedWindow->Frame().LeftTop();
	else {
		if (fSelectedWindow->Anchor(fSelectedWorkspace).position
				== kInvalidWindowPosition) {
			fSelectedWindow->Anchor(fSelectedWorkspace).position
				= fSelectedWindow->Frame().LeftTop();
		}
		leftTop = fSelectedWindow->Anchor(fSelectedWorkspace).position;
	}

	// Don't treat every little mouse move as a window move - this would
	// make it too hard to activate a workspace.
	float diff = max_c(fabs(fClickPoint.x - where.x),
		fabs(fClickPoint.y - where.y));
	if (!fHasMoved && diff > 2)
		fHasMoved = true;

	if (fHasMoved) {
		Window()->Desktop()->MoveWindowBy(fSelectedWindow, left - leftTop.x,
			top - leftTop.y, fSelectedWorkspace);
	}
}


/** @brief Notifies the view that a window's state has changed.
 *
 *  Triggers a full repaint of the workspaces view.  A more targeted
 *  implementation could narrow the dirty region, but this ensures correctness.
 *
 *  @param window The window whose state changed (currently unused).
 */
void
WorkspacesView::WindowChanged(::Window* window)
{
	// TODO: be smarter about this!
	_Invalidate();
}


/** @brief Notifies the view that a window has been removed from the desktop.
 *
 *  Clears the selected-window pointer if it refers to the window being removed
 *  to prevent use-after-free during subsequent mouse events.
 *
 *  @param window The window that has been removed.
 */
void
WorkspacesView::WindowRemoved(::Window* window)
{
	if (fSelectedWindow == window)
		fSelectedWindow = NULL;
}
