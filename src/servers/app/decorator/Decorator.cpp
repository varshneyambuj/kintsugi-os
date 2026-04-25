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
 *   Copyright 2001-2020 Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       John Scipione, jscipione@gmail.com
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *       Clemens Zeidler, haiku@clemens-zeidler.de
 *       Joseph Groover, looncraz@looncraz.net
 *       Tri-Edge AI
 *       Jacob Secunda, secundja@gmail.com
 */


/**
 * @file Decorator.cpp
 * @brief Base class for window decorators in the app_server.
 *
 * Provides the per-window state, hit-testing, and footprint machinery that all
 * concrete decorators (TabDecorator, DefaultDecorator, third-party add-ons)
 * build on. Subclasses are responsible for the actual painting of frames,
 * tabs, titles, and buttons; this class manages tab lists, focus, sizes,
 * region highlights, and coordinate transforms during move and resize.
 *
 * @see TabDecorator, DefaultDecorator, DecorManager
 */


#include "Decorator.h"

#include <stdio.h>

#include <Region.h>

#include "Desktop.h"
#include "DesktopSettings.h"
#include "DrawingEngine.h"


/**
 * @brief Constructs an empty tab with neutral defaults.
 *
 * Rectangles are zero-area, the look defaults to a titled window, no buttons
 * are pressed, the tab is unfocused, and all four cached button-bitmap slots
 * (per state) are cleared.
 */
Decorator::Tab::Tab()
	:
	tabRect(),

	zoomRect(),
	closeRect(),
	minimizeRect(),

	closePressed(false),
	zoomPressed(false),
	minimizePressed(false),

	look(B_TITLED_WINDOW_LOOK),
	flags(0),
	isFocused(false),
	title(""),

	tabOffset(0),
	tabLocation(0.0f),
	textOffset(10.0f),

	truncatedTitle(""),
	truncatedTitleLength(0),

	buttonFocus(false),
	isHighlighted(false),

	minTabSize(0.0f),
	maxTabSize(0.0f)
{
	closeBitmaps[0] = closeBitmaps[1] = closeBitmaps[2] = closeBitmaps[3]
		= minimizeBitmaps[0] = minimizeBitmaps[1] = minimizeBitmaps[2]
		= minimizeBitmaps[3] = zoomBitmaps[0] = zoomBitmaps[1] = zoomBitmaps[2]
		= zoomBitmaps[3] = NULL;
}


/**
 * @brief Constructs a Decorator bound to a desktop and an initial client frame.
 *
 * Initializes all border, frame, outline, and tab-list state to empty so that
 * a subclass's _DoLayout() can populate them once a DrawingEngine is attached.
 * Region highlights are cleared.
 *
 * @param settings Snapshot of current desktop settings (consulted by
 *                 subclasses for fonts and colors).
 * @param frame    Initial client-area frame in screen coordinates.
 * @param desktop  Desktop the decorated window lives on.
 */
Decorator::Decorator(DesktopSettings& settings, BRect frame,
					Desktop* desktop)
	:
	fLocker("Decorator"),

	fDrawingEngine(NULL),
	fDrawState(),

	fTitleBarRect(),
	fFrame(frame),
	fResizeRect(),
	fBorderRect(),
	fOutlineBorderRect(),

	fLeftBorder(),
	fTopBorder(),
	fBottomBorder(),
	fRightBorder(),

	fLeftOutlineBorder(),
	fTopOutlineBorder(),
	fBottomOutlineBorder(),
	fRightOutlineBorder(),

	fBorderWidth(-1),
	fOutlineBorderWidth(-1),

	fTopTab(NULL),

	fDesktop(desktop),
	fFootprintValid(false)
{
	memset(&fRegionHighlights, HIGHLIGHT_NONE, sizeof(fRegionHighlights));
}


/**
 * @brief Destroys the Decorator. Tabs owned by fTabList are deleted by
 *        BObjectList.
 */
Decorator::~Decorator()
{
}


/**
 * @brief Adds a new tab to the decorated window.
 *
 * The new tab becomes the top tab and is registered with the subclass via
 * _AddTab(). On allocation or insertion failure the previous top tab is
 * restored.
 *
 * @param settings     Current desktop settings (for fonts/colors during layout).
 * @param title        Initial title for the new tab.
 * @param look         Window look applied to the new tab.
 * @param flags        Window flags applied to the new tab.
 * @param index        Insertion index, or -1 to append.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 * @return Pointer to the newly created tab, or NULL on failure.
 */
Decorator::Tab*
Decorator::AddTab(DesktopSettings& settings, const char* title,
	window_look look, uint32 flags, int32 index, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* tab = _AllocateNewTab();
	if (tab == NULL)
		return NULL;
	tab->title = title;
	tab->look = look;
	tab->flags = flags;

	bool ok = false;
	if (index >= 0) {
		if (fTabList.AddItem(tab, index) == true)
			ok = true;
	} else if (fTabList.AddItem(tab) == true)
		ok = true;

	if (ok == false) {
		delete tab;
		return NULL;
	}

	Decorator::Tab* oldTop = fTopTab;
	fTopTab = tab;
	if (_AddTab(settings, index, updateRegion) == false) {
		fTabList.RemoveItem(tab);
		delete tab;
		fTopTab = oldTop;
		return NULL;
	}

	_InvalidateFootprint();
	return tab;
}


/**
 * @brief Removes the tab at @a index.
 *
 * The removed tab's rectangle is added to @a updateRegion so the desktop can
 * repaint the area it occupied.
 *
 * @param index        Index of the tab to remove.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 * @return true if a tab was removed, false if @a index was out of range.
 */
bool
Decorator::RemoveTab(int32 index, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	// add removed tab area to update region before removing it
	if (updateRegion != NULL)
		updateRegion->Include(TabRect(index));

	Decorator::Tab* tab = fTabList.RemoveItemAt(index);
	if (tab == NULL)
		return false;

	_RemoveTab(index, updateRegion);

	delete tab;
	_InvalidateFootprint();
	return true;
}


/**
 * @brief Reorders a tab within the tab list.
 *
 * Asks the subclass (_MoveTab) to update its layout first, then commits the
 * list reorder. If the list reorder fails the subclass move is rolled back.
 *
 * @param from         Source index.
 * @param to           Destination index.
 * @param isMoving     true while the user is interactively dragging the tab,
 *                     false on the final commit.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 * @return true on success, false on subclass refusal or list-move failure.
 */
bool
Decorator::MoveTab(int32 from, int32 to, bool isMoving, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	if (_MoveTab(from, to, isMoving, updateRegion) == false)
		return false;
	if (fTabList.MoveItem(from, to) == false) {
		// move the tab back
		_MoveTab(from, to, isMoving, updateRegion);
		return false;
	}
	return true;
}


/**
 * @brief Returns the index of the tab whose tab-rectangle contains @a where.
 *
 * @param where Hit-test point in screen coordinates.
 * @return Tab index, or -1 if no tab is hit.
 */
int32
Decorator::TabAt(const BPoint& where) const
{
	AutoReadLocker _(fLocker);

	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (tab->tabRect.Contains(where))
			return i;
	}

	return -1;
}


/**
 * @brief Selects which tab is currently shown on top of the stack.
 *
 * @param tab Tab index; if out of range, fTopTab is set to NULL.
 */
void
Decorator::SetTopTab(int32 tab)
{
	AutoWriteLocker _(fLocker);
	fTopTab = fTabList.ItemAt(tab);
}


/**
 * @brief Binds the decorator to the DrawingEngine it should paint into.
 *
 * Many subclass layouts depend on string measurement, which requires a live
 * engine; once attached this method runs both the regular and outline layout
 * passes.
 *
 * @param engine DrawingEngine instance, or NULL to detach.
 */
void
Decorator::SetDrawingEngine(DrawingEngine* engine)
{
	AutoWriteLocker _(fLocker);

	fDrawingEngine = engine;
	// lots of subclasses will depend on the driver for text support, so call
	// _DoLayout() after we have it
	if (fDrawingEngine != NULL) {
		_DoLayout();
		_DoOutlineLayout();
	}
}


/**
 * @brief Updates the window flags for a tab.
 *
 * Synchronizes the B_NOT_RESIZABLE / B_NOT_H_RESIZABLE / B_NOT_V_RESIZABLE
 * flags so subclasses do not need to handle every combination, then forwards
 * to the subclass _SetFlags() hook.
 *
 * @param tab          Tab index whose flags are being updated.
 * @param flags        New flags value.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 *
 * @note Does not redraw on its own; the caller is responsible for repainting.
 */
void
Decorator::SetFlags(int32 tab, uint32 flags, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	// we're nice to our subclasses - we make sure B_NOT_{H|V|}_RESIZABLE
	// are in sync (it's only a semantical simplification, not a necessity)
	if ((flags & (B_NOT_H_RESIZABLE | B_NOT_V_RESIZABLE))
			== (B_NOT_H_RESIZABLE | B_NOT_V_RESIZABLE))
		flags |= B_NOT_RESIZABLE;
	if (flags & B_NOT_RESIZABLE)
		flags |= B_NOT_H_RESIZABLE | B_NOT_V_RESIZABLE;

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;
	_SetFlags(decoratorTab, flags, updateRegion);
	_InvalidateFootprint();
		// the border might have changed (smaller/larger tab)
}


/**
 * @brief Notifies the decorator that the system fonts have changed.
 *
 * @param settings     Updated desktop settings.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 */
void
Decorator::FontsChanged(DesktopSettings& settings, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	_FontsChanged(settings, updateRegion);
	_InvalidateFootprint();
}


/**
 * @brief Notifies the decorator that the system color set has changed.
 *
 * Re-runs UpdateColors() and invalidates cached button bitmaps so they are
 * regenerated with the new palette.
 *
 * @param settings     Updated desktop settings.
 * @param updateRegion Optional dirty region; if non-NULL, the decorator
 *                     footprint is added to it.
 */
void
Decorator::ColorsChanged(DesktopSettings& settings, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	UpdateColors(settings);

	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());

	_InvalidateBitmaps();
}


/**
 * @brief Changes the window look for one tab.
 *
 * @param tab          Tab index.
 * @param settings     Current desktop settings (for font/look changes).
 * @param look         New window look value.
 * @param updateRect   Optional dirty region to be extended; may be NULL.
 */
void
Decorator::SetLook(int32 tab, DesktopSettings& settings, window_look look,
	BRegion* updateRect)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	_SetLook(decoratorTab, settings, look, updateRect);
	_InvalidateFootprint();
		// the border very likely changed
}


/**
 * @brief Returns the window look for a tab.
 *
 * @param tab Tab index.
 * @return The tab's window_look value.
 */
window_look
Decorator::Look(int32 tab) const
{
	AutoReadLocker _(fLocker);
	return TabAt(tab)->look;
}


/**
 * @brief Returns the window flags for a tab.
 *
 * @param tab Tab index.
 * @return The tab's flags value.
 */
uint32
Decorator::Flags(int32 tab) const
{
	AutoReadLocker _(fLocker);
	return TabAt(tab)->flags;
}


/**
 * @brief Returns the rectangle bounding the entire decorator border.
 *
 * @return Border rectangle in screen coordinates.
 */
BRect
Decorator::BorderRect() const
{
	AutoReadLocker _(fLocker);
	return fBorderRect;
}


/**
 * @brief Returns the rectangle bounding the title bar (the union of all tabs).
 *
 * @return Title-bar rectangle in screen coordinates.
 */
BRect
Decorator::TitleBarRect() const
{
	AutoReadLocker _(fLocker);
	return fTitleBarRect;
}


/**
 * @brief Returns the rectangle bounding a specific tab.
 *
 * @param tab Tab index.
 * @return Tab rectangle in screen coordinates, or an invalid rect if @a tab
 *         is out of range.
 */
BRect
Decorator::TabRect(int32 tab) const
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return BRect();
	return decoratorTab->tabRect;
}


/**
 * @brief Returns the rectangle of @a tab.
 *
 * @param tab Tab object whose rectangle is requested.
 * @return Tab rectangle in screen coordinates.
 */
BRect
Decorator::TabRect(Decorator::Tab* tab) const
{
	return tab->tabRect;
}


/**
 * @brief Toggles the visual "pressed" state of the close button on a tab.
 *
 * Triggers a redraw of the close button when the state changes.
 *
 * @param tab     Tab index.
 * @param pressed true if the close button should appear pressed.
 */
void
Decorator::SetClose(int32 tab, bool pressed)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	if (pressed != decoratorTab->closePressed) {
		decoratorTab->closePressed = pressed;
		DrawClose(tab);
	}
}


/**
 * @brief Toggles the visual "pressed" state of the minimize button on a tab.
 *
 * Triggers a redraw of the minimize button when the state changes.
 *
 * @param tab     Tab index.
 * @param pressed true if the minimize button should appear pressed.
 */
void
Decorator::SetMinimize(int32 tab, bool pressed)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	if (pressed != decoratorTab->minimizePressed) {
		decoratorTab->minimizePressed = pressed;
		DrawMinimize(tab);
	}
}

/**
 * @brief Toggles the visual "pressed" state of the zoom button on a tab.
 *
 * Triggers a redraw of the zoom button when the state changes.
 *
 * @param tab     Tab index.
 * @param pressed true if the zoom button should appear pressed.
 */
void
Decorator::SetZoom(int32 tab, bool pressed)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	if (pressed != decoratorTab->zoomPressed) {
		decoratorTab->zoomPressed = pressed;
		DrawZoom(tab);
	}
}


/**
 * @brief Updates the title of a tab and asks the subclass to relayout.
 *
 * @param tab          Tab index.
 * @param string       New title (copied).
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 *
 * @todo Trigger a redraw automatically.
 */
void
Decorator::SetTitle(int32 tab, const char* string, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	decoratorTab->title.SetTo(string);
	_SetTitle(decoratorTab, string, updateRegion);

	_InvalidateFootprint();
		// the border very likely changed

	// TODO: redraw?
}


/**
 * @brief Returns the title of a tab by index.
 *
 * @param tab Tab index.
 * @return Title text, or "" if @a tab is out of range.
 */
const char*
Decorator::Title(int32 tab) const
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return "";

	return decoratorTab->title;
}


/**
 * @brief Returns the title of the given tab.
 *
 * @param tab Tab object.
 * @return Title text.
 */
const char*
Decorator::Title(Decorator::Tab* tab) const
{
	AutoReadLocker _(fLocker);
	return tab->title;
}


/**
 * @brief Returns the horizontal pixel offset of a tab inside the title bar.
 *
 * @param tab Tab index.
 * @return Tab offset in pixels, or 0 if @a tab is out of range.
 */
float
Decorator::TabLocation(int32 tab) const
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = _TabAt(tab);
	if (decoratorTab == NULL)
		return 0.0f;

	return (float)decoratorTab->tabOffset;
}


/**
 * @brief Sets the horizontal pixel offset of a tab inside the title bar.
 *
 * Forwards to the subclass _SetTabLocation() hook and invalidates the cached
 * footprint when the location actually changes.
 *
 * @param tab          Tab index.
 * @param location     New horizontal offset, in pixels from the left border.
 * @param isShifting   true while interactively dragging a tab; false on commit.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 * @return true if the tab location was updated; false if out of bounds or
 *         the subclass refused.
 */
bool
Decorator::SetTabLocation(int32 tab, float location, bool isShifting,
	BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return false;
	if (_SetTabLocation(decoratorTab, location, isShifting, updateRegion)) {
		_InvalidateFootprint();
		return true;
	}
	return false;
}



/**
 * @brief Updates the focus state of a tab.
 *
 * Records the new state and forwards to the subclass _SetFocus() hook so it
 * can swap focus colors. The decorator does not redraw on its own.
 *
 * @param tab    Tab index.
 * @param active true if the tab now has focus.
 *
 * @todo Consider performing the redraw here for symmetry with other setters.
 */
void
Decorator::SetFocus(int32 tab, bool active)
{
	AutoWriteLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;
	decoratorTab->isFocused = active;
	_SetFocus(decoratorTab);
	// TODO: maybe it would be cleaner to handle the redraw here.
}


/**
 * @brief Returns whether a tab currently holds focus.
 *
 * @param tab Tab index.
 * @return true if the tab is focused; false if @a tab is out of range or not
 *         focused.
 */
bool
Decorator::IsFocus(int32 tab) const
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return false;

	return decoratorTab->isFocused;
};


/**
 * @brief Returns whether the given tab is focused.
 *
 * @param tab Tab object.
 * @return true if @a tab has focus.
 */
bool
Decorator::IsFocus(Decorator::Tab* tab) const
{
	AutoReadLocker _(fLocker);
	return tab->isFocused;
}


//	#pragma mark - virtual methods


/**
 * @brief Returns the screen-space region covered by the decorator.
 *
 * The footprint is cached and only recomputed when invalidated. When an
 * interactive outline-resize is in progress the outline borders are
 * included in the returned region.
 *
 * @return Reference to the cached footprint region.
 */
const BRegion&
Decorator::GetFootprint()
{
	AutoReadLocker _(fLocker);

	if (!fFootprintValid) {
		fFootprint.MakeEmpty();

		_GetFootprint(&fFootprint);

		if (IsOutlineResizing())
			_GetOutlineFootprint(&fFootprint);

		fFootprintValid = true;
	}

	return fFootprint;
}


/**
 * @brief Returns the desktop the decorator was created on.
 *
 * @return Pointer to the owning Desktop.
 */
::Desktop*
Decorator::GetDesktop()
{
	AutoReadLocker _(fLocker);
	return fDesktop;
}


/**
 * @brief Hit-tests @a where against the decorator regions.
 *
 * The base class only resolves hits on tabs and tab buttons. Subclasses such
 * as TabDecorator override this to additionally identify hits on borders and
 * resize corners.
 *
 * @param where    Point to test, in screen coordinates.
 * @param tabIndex Out-parameter set to the index of the hit tab, or -1.
 * @return One of the Region constants:
 *     - REGION_NONE: no decorator region was hit.
 *     - REGION_TAB / REGION_CLOSE_BUTTON / REGION_ZOOM_BUTTON: hit on the tab
 *       or one of its embedded buttons.
 */
Decorator::Region
Decorator::RegionAt(BPoint where, int32& tabIndex) const
{
	AutoReadLocker _(fLocker);

	tabIndex = -1;

	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (tab->closeRect.Contains(where)) {
			tabIndex = i;
			return REGION_CLOSE_BUTTON;
		}
		if (tab->zoomRect.Contains(where)) {
			tabIndex = i;
			return REGION_ZOOM_BUTTON;
		}
		if (tab->tabRect.Contains(where)) {
			tabIndex = i;
			return REGION_TAB;
		}
	}

	return REGION_NONE;
}


/**
 * @brief Moves the decorator frame and all dependent rectangles.
 *
 * @param x Horizontal offset, in pixels.
 * @param y Vertical offset, in pixels.
 *
 * @note Subclasses overriding _MoveBy should chain to this implementation so
 *       all base members are translated.
 */
void
Decorator::MoveBy(float x, float y)
{
	MoveBy(BPoint(x, y));
}


/**
 * @brief Moves the decorator frame and all dependent rectangles by @a offset.
 *
 * Updates the cached footprint in place when valid, then forwards to the
 * subclass _MoveBy() and _MoveOutlineBy() hooks.
 *
 * @param offset Move offset in pixels.
 */
void
Decorator::MoveBy(BPoint offset)
{
	AutoWriteLocker _(fLocker);

	if (fFootprintValid)
		fFootprint.OffsetBy(offset.x, offset.y);

	_MoveBy(offset);
	_MoveOutlineBy(offset);
}


/**
 * @brief Resizes the decorator frame.
 *
 * Forwards to the BPoint overload. Subclasses must implement _ResizeBy() to
 * relayout in response to size changes; this base implementation does no
 * additional bookkeeping beyond invalidating the footprint.
 *
 * @param x     Horizontal size delta, in pixels.
 * @param y     Vertical size delta, in pixels.
 * @param dirty Region extended with areas that need repainting; may be NULL.
 */
void
Decorator::ResizeBy(float x, float y, BRegion* dirty)
{
	ResizeBy(BPoint(x, y), dirty);
}


/**
 * @brief Resizes the decorator frame by @a offset.
 *
 * Calls the subclass _ResizeBy() and _ResizeOutlineBy() hooks and then
 * invalidates the cached footprint.
 *
 * @param offset Size delta, in pixels.
 * @param dirty  Region extended with areas that need repainting; may be NULL.
 */
void
Decorator::ResizeBy(BPoint offset, BRegion* dirty)
{
	AutoWriteLocker _(fLocker);

	_ResizeBy(offset, dirty);
	_ResizeOutlineBy(offset, dirty);

	_InvalidateFootprint();
}


/**
 * @brief Sets the outline-resize delta during interactive border drag.
 *
 * @param delta Cumulative outline offset, in pixels.
 * @param dirty Region extended with the moved outline borders.
 */
void
Decorator::SetOutlinesDelta(BPoint delta, BRegion* dirty)
{
	_SetOutlinesDelta(delta, dirty);
	_InvalidateFootprint();
}


/**
 * @brief Extends @a dirty with the rectangle(s) belonging to a logical region.
 *
 * Used to mark the smallest meaningful subset of the decorator for repaint
 * when only a single button or border has changed.
 *
 * @param region Logical region whose paint area should be invalidated.
 * @param dirty  Region to be extended.
 */
void
Decorator::ExtendDirtyRegion(Region region, BRegion& dirty)
{
	AutoReadLocker _(fLocker);

	switch (region) {
		case REGION_TAB:
			dirty.Include(fTitleBarRect);
			break;

		case REGION_CLOSE_BUTTON:
			if ((fTopTab->flags & B_NOT_CLOSABLE) == 0) {
				for (int32 i = 0; i < fTabList.CountItems(); i++)
					dirty.Include(fTabList.ItemAt(i)->closeRect);
			}
			break;

		case REGION_MINIMIZE_BUTTON:
			if ((fTopTab->flags & B_NOT_MINIMIZABLE) == 0) {
				for (int32 i = 0; i < fTabList.CountItems(); i++)
					dirty.Include(fTabList.ItemAt(i)->minimizeRect);
			}
			break;

		case REGION_ZOOM_BUTTON:
			if ((fTopTab->flags & B_NOT_ZOOMABLE) == 0) {
				for (int32 i = 0; i < fTabList.CountItems(); i++)
					dirty.Include(fTabList.ItemAt(i)->zoomRect);
			}
			break;

		case REGION_LEFT_BORDER:
			if (fLeftBorder.IsValid()) {
				// fLeftBorder doesn't include the corners, so we have to add
				// them manually.
				BRect rect(fLeftBorder);
				rect.top = fTopBorder.top;
				rect.bottom = fBottomBorder.bottom;
				dirty.Include(rect);
			}
			break;

		case REGION_RIGHT_BORDER:
			if (fRightBorder.IsValid()) {
				// fRightBorder doesn't include the corners, so we have to add
				// them manually.
				BRect rect(fRightBorder);
				rect.top = fTopBorder.top;
				rect.bottom = fBottomBorder.bottom;
				dirty.Include(rect);
			}
			break;

		case REGION_TOP_BORDER:
			dirty.Include(fTopBorder);
			break;

		case REGION_BOTTOM_BORDER:
			dirty.Include(fBottomBorder);
			break;

		case REGION_RIGHT_BOTTOM_CORNER:
			if ((fTopTab->flags & B_NOT_RESIZABLE) == 0)
				dirty.Include(fResizeRect);
			break;

		default:
			break;
	}
}


/**
 * @brief Applies a highlight value to a decorator region.
 *
 * Subclasses may override to additionally invalidate cached imagery for the
 * affected region; the base implementation must still be called.
 *
 * @param region    Region whose highlight is being changed.
 * @param highlight Highlight kind (HIGHLIGHT_NONE, HIGHLIGHT_RESIZE_BORDER,
 *                  or a user-defined value).
 * @param dirty     Region to be extended when the highlight changes; may be
 *                  NULL.
 * @param tab       Tab index (unused by base implementation).
 * @return true if the highlight is now applied (including no-op equality);
 *         false if @a region is invalid.
 */
bool
Decorator::SetRegionHighlight(Region region, uint8 highlight, BRegion* dirty,
	int32 tab)
{
	AutoWriteLocker _(fLocker);

	int32 index = (int32)region - 1;
	if (index < 0 || index >= REGION_COUNT - 1)
		return false;

	if (fRegionHighlights[index] == highlight)
		return true;
	fRegionHighlights[index] = highlight;

	if (dirty != NULL)
		ExtendDirtyRegion(region, *dirty);

	return true;
}


/**
 * @brief Restores decorator settings (e.g. tab locations) from a flattened
 *        BMessage.
 *
 * Forwards to the subclass _SetSettings() hook and invalidates the cached
 * footprint when any setting actually changes.
 *
 * @param settings     Flattened settings message produced by GetSettings().
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 * @return true if any setting was applied, false otherwise.
 */
bool
Decorator::SetSettings(const BMessage& settings, BRegion* updateRegion)
{
	AutoWriteLocker _(fLocker);

	if (_SetSettings(settings, updateRegion)) {
		_InvalidateFootprint();
		return true;
	}
	return false;
}


/**
 * @brief Flattens decorator settings (tab frame, border width, per-tab
 *        offsets) into @a settings.
 *
 * @param settings Out-parameter; receives the flattened settings.
 * @return true on success, false if the title bar is invalid or a write
 *         operation failed.
 *
 * @todo Restrict the per-tab location entries to the requesting window's
 *       tab only.
 */
bool
Decorator::GetSettings(BMessage* settings) const
{
	AutoReadLocker _(fLocker);

	if (!fTitleBarRect.IsValid())
		return false;

	if (settings->AddRect("tab frame", fTitleBarRect) != B_OK)
		return false;

	if (settings->AddFloat("border width", fBorderWidth) != B_OK)
		return false;

	// TODO only add the location of the tab of the window who requested the
	// settings
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = _TabAt(i);
		if (settings->AddFloat("tab location", (float)tab->tabOffset) != B_OK)
			return false;
	}

	return true;
}


/**
 * @brief Constrains the supplied minimum size limits to accommodate the
 *        decorator chrome.
 *
 * The current minimum is widened to fit the smallest tab and tall enough for
 * the resize knob; maxima are not modified.
 *
 * @param minWidth  In/out minimum width of the decorated window.
 * @param minHeight In/out minimum height of the decorated window.
 * @param maxWidth  Maximum width (currently not modified by base class).
 * @param maxHeight Maximum height (currently not modified by base class).
 */
void
Decorator::GetSizeLimits(int32* minWidth, int32* minHeight,
	int32* maxWidth, int32* maxHeight) const
{
	AutoReadLocker _(fLocker);

	float minTabSize = 0;
	if (CountTabs() > 0)
		minTabSize = _TabAt(0)->minTabSize;

	if (fTitleBarRect.IsValid()) {
		*minWidth = (int32)roundf(max_c(*minWidth,
			minTabSize - 2 * fBorderWidth));
	}
	if (fResizeRect.IsValid()) {
		*minHeight = (int32)roundf(max_c(*minHeight,
			fResizeRect.Height() - fBorderWidth));
	}
}


/**
 * @brief Draws the tab body, its zoom and minimize buttons, the title text,
 *        and finally the close button for a single tab.
 *
 * @param tabIndex Tab index; calls are silently ignored for invalid indices.
 */
void
Decorator::DrawTab(int32 tabIndex)
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* tab = fTabList.ItemAt(tabIndex);
	if (tab == NULL)
		return;

	_DrawTab(tab, tab->tabRect);
	_DrawZoom(tab, false, tab->zoomRect);
	_DrawMinimize(tab, false, tab->minimizeRect);
	_DrawTitle(tab, tab->tabRect);
	_DrawClose(tab, false, tab->closeRect);
}


/**
 * @brief Draws only the title text of a tab.
 *
 * @param tab Tab index; ignored if out of range.
 */
void
Decorator::DrawTitle(int32 tab)
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;
	_DrawTitle(decoratorTab, decoratorTab->tabRect);
}


/**
 * @brief Draws the close button of a tab in its current pressed/unpressed
 *        state.
 *
 * @param tab Tab index; ignored if out of range.
 */
void
Decorator::DrawClose(int32 tab)
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	_DrawClose(decoratorTab, true, decoratorTab->closeRect);
}


/**
 * @brief Draws the minimize button of a tab.
 *
 * @param tab Tab index; ignored if out of range.
 *
 * @note The default decorator delegates this to _DrawTab on the minimize
 *       rectangle. Subclasses with a real minimize button typically override
 *       _DrawMinimize().
 */
void
Decorator::DrawMinimize(int32 tab)
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;

	_DrawTab(decoratorTab, decoratorTab->minimizeRect);
}


/**
 * @brief Draws the zoom button of a tab in its current pressed/unpressed
 *        state.
 *
 * @param tab Tab index; ignored if out of range.
 */
void
Decorator::DrawZoom(int32 tab)
{
	AutoReadLocker _(fLocker);

	Decorator::Tab* decoratorTab = fTabList.ItemAt(tab);
	if (decoratorTab == NULL)
		return;
	_DrawZoom(decoratorTab, true, decoratorTab->zoomRect);
}


/**
 * @brief Resolves a UI-color identifier to its current rgb value via desktop
 *        settings.
 *
 * @param which The color_which constant to resolve.
 * @return The corresponding rgb_color from the desktop color set.
 */
rgb_color
Decorator::UIColor(color_which which)
{
	AutoReadLocker _(fLocker);
	DesktopSettings settings(fDesktop);
	return settings.UIColor(which);
}


/**
 * @brief Returns the current border width, in pixels.
 *
 * @return Border width as set by the most recent layout pass.
 */
float
Decorator::BorderWidth()
{
	AutoReadLocker _(fLocker);
	return fBorderWidth;
}


/**
 * @brief Returns the height of the title-bar tab, falling back to border
 *        width when no tab is present.
 *
 * @return Tab height in pixels.
 */
float
Decorator::TabHeight()
{
	AutoReadLocker _(fLocker);

	if (fTitleBarRect.IsValid())
		return fTitleBarRect.Height();

	return fBorderWidth;
}


// #pragma mark - Protected methods


/**
 * @brief Allocates and returns a new Decorator::Tab with default colors.
 *
 * Subclasses override this hook to allocate their own enriched tab type.
 *
 * @return Pointer to the new tab, or NULL on allocation failure.
 */
Decorator::Tab*
Decorator::_AllocateNewTab()
{
	Decorator::Tab* tab = new(std::nothrow) Decorator::Tab;
	if (tab == NULL)
		return NULL;

	// Set appropriate colors based on the current focus value. In this case,
	// each decorator defaults to not having the focus.
	_SetFocus(tab);
	return tab;
}


/**
 * @brief Draws all tabs into @a rect, painting the focused tab last so it
 *        appears on top.
 *
 * @param rect Clip rectangle for the tab drawing.
 */
void
Decorator::_DrawTabs(BRect rect)
{
	Decorator::Tab* focusTab = NULL;
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (tab->isFocused) {
			focusTab = tab;
			continue;
		}
		_DrawTab(tab, rect);
	}

	if (focusTab != NULL)
		_DrawTab(focusTab, rect);
}


/**
 * @brief Hook invoked when a tab's focus state changes.
 *
 * Subclasses override this to swap focus colors. The base implementation is
 * a no-op.
 *
 * @param tab Tab whose focus state was just updated.
 */
void
Decorator::_SetFocus(Decorator::Tab* tab)
{
}


/**
 * @brief Subclass hook for repositioning a tab during a slide gesture.
 *
 * The base class does not implement tab sliding and always returns false.
 *
 * @param tab        Tab being moved.
 * @param location   New horizontal offset.
 * @param isShifting true while the user is mid-drag.
 * @return Always false in the base class.
 */
bool
Decorator::_SetTabLocation(Decorator::Tab* tab, float location, bool isShifting,
	BRegion* /*updateRegion*/)
{
	return false;
}


/**
 * @brief Returns the tab at @a index without locking.
 *
 * @param index Tab index.
 * @return Tab pointer, or NULL if out of range.
 */
Decorator::Tab*
Decorator::_TabAt(int32 index) const
{
	return static_cast<Decorator::Tab*>(fTabList.ItemAt(index));
}


/**
 * @brief Default implementation of the font-changed hook.
 *
 * Adds the old footprint to @a updateRegion, drops cached button bitmaps,
 * re-runs the subclass _UpdateFont() and layout passes, then adds the new
 * footprint to @a updateRegion.
 *
 * @param settings     Updated desktop settings.
 * @param updateRegion Optional dirty region; may be NULL.
 */
void
Decorator::_FontsChanged(DesktopSettings& settings, BRegion* updateRegion)
{
	// get previous extent
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());

	_InvalidateBitmaps();

	_UpdateFont(settings);
	_DoLayout();
	_DoOutlineLayout();

	_InvalidateFootprint();
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());
}


/**
 * @brief Default implementation of the look-changed hook.
 *
 * Updates the tab's look field, refreshes fonts and re-runs layout. The old
 * and new footprints are added to @a updateRegion so the desktop can repaint
 * any newly exposed area.
 *
 * @param tab          Tab whose look is being changed.
 * @param settings     Current desktop settings.
 * @param look         New window look.
 * @param updateRegion Optional dirty region; may be NULL.
 */
void
Decorator::_SetLook(Decorator::Tab* tab, DesktopSettings& settings,
	window_look look, BRegion* updateRegion)
{
	// TODO: we could be much smarter about the update region

	// get previous extent
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());

	tab->look = look;

	_UpdateFont(settings);
	_DoLayout();
	_DoOutlineLayout();

	_InvalidateFootprint();
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());
}


/**
 * @brief Default implementation of the flags-changed hook.
 *
 * Records the new flags on @a tab and re-runs layout. The old and new
 * footprints are added to @a updateRegion.
 *
 * @param tab          Tab whose flags are being changed.
 * @param flags        New flags value.
 * @param updateRegion Optional dirty region; may be NULL.
 */
void
Decorator::_SetFlags(Decorator::Tab* tab, uint32 flags, BRegion* updateRegion)
{
	// TODO: we could be much smarter about the update region

	// get previous extent
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());

	tab->flags = flags;
	_DoLayout();
	_DoOutlineLayout();

	_InvalidateFootprint();
	if (updateRegion != NULL)
		updateRegion->Include(&GetFootprint());
}


/**
 * @brief Default move hook: translates all per-tab rectangles, the title
 *        bar, frame, resize rect, and border rect by @a offset.
 *
 * Subclasses override this hook to additionally translate any rectangles
 * they introduce (e.g. four-sided border rectangles).
 *
 * @param offset Move offset in pixels.
 */
void
Decorator::_MoveBy(BPoint offset)
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);

		tab->zoomRect.OffsetBy(offset);
		tab->closeRect.OffsetBy(offset);
		tab->minimizeRect.OffsetBy(offset);
		tab->tabRect.OffsetBy(offset);
	}
	fTitleBarRect.OffsetBy(offset);
	fFrame.OffsetBy(offset);
	fResizeRect.OffsetBy(offset);
	fBorderRect.OffsetBy(offset);
}


/**
 * @brief Translates all outline rectangles used during interactive border
 *        resize by @a offset.
 *
 * @param offset Move offset in pixels.
 */
void
Decorator::_MoveOutlineBy(BPoint offset)
{
	fOutlineBorderRect.OffsetBy(offset);

	fLeftOutlineBorder.OffsetBy(offset);
	fRightOutlineBorder.OffsetBy(offset);
	fTopOutlineBorder.OffsetBy(offset);
	fBottomOutlineBorder.OffsetBy(offset);
}


/**
 * @brief Resizes the outline rectangles (used during outline-resize) by
 *        @a offset.
 *
 * @param offset Size delta in pixels.
 * @param dirty  Region to be extended; not modified by the base implementation.
 */
void
Decorator::_ResizeOutlineBy(BPoint offset, BRegion* dirty)
{
	fOutlineBorderRect.right += offset.x;
	fOutlineBorderRect.bottom += offset.y;

	fLeftOutlineBorder.bottom += offset.y;
	fTopOutlineBorder.right += offset.x;

	fRightOutlineBorder.OffsetBy(offset.x, 0.0);
	fRightOutlineBorder.bottom += offset.y;

	fBottomOutlineBorder.OffsetBy(0.0, offset.y);
	fBottomOutlineBorder.right += offset.x;
}


/**
 * @brief Applies a new outline-resize delta and updates @a dirty so that
 *        both the previous and new outline border positions are repainted.
 *
 * @param delta Cumulative outline offset in pixels.
 * @param dirty Region extended with the union of old and new outline borders.
 */
void
Decorator::_SetOutlinesDelta(BPoint delta, BRegion* dirty)
{
	BPoint offset = delta - fOutlinesDelta;
	fOutlinesDelta = delta;

	dirty->Include(fLeftOutlineBorder);
	dirty->Include(fRightOutlineBorder);
	dirty->Include(fTopOutlineBorder);
	dirty->Include(fBottomOutlineBorder);

	fOutlineBorderRect.right += offset.x;
	fOutlineBorderRect.bottom += offset.y;

	fLeftOutlineBorder.bottom += offset.y;
	fTopOutlineBorder.right += offset.x;

	fRightOutlineBorder.OffsetBy(offset.x, 0.0);
	fRightOutlineBorder.bottom	+= offset.y;

	fBottomOutlineBorder.OffsetBy(0.0, offset.y);
	fBottomOutlineBorder.right	+= offset.x;

	dirty->Include(fLeftOutlineBorder);
	dirty->Include(fRightOutlineBorder);
	dirty->Include(fTopOutlineBorder);
	dirty->Include(fBottomOutlineBorder);
}


/**
 * @brief Subclass hook for restoring decorator settings.
 *
 * Base class returns false (no settings consumed); subclasses such as
 * TabDecorator override this to read tab locations and similar state.
 *
 * @param settings     Flattened settings message.
 * @param updateRegion Optional dirty region; may be NULL.
 * @return Always false in the base implementation.
 */
bool
Decorator::_SetSettings(const BMessage& settings, BRegion* updateRegion)
{
	return false;
}


/**
 * @brief Default footprint hook: subclasses override to fill @a region with
 *        the screen-space area occupied by the decorator.
 *
 * @param region Region to be populated.
 */
void
Decorator::_GetFootprint(BRegion *region)
{
}


/**
 * @brief Includes the four outline-border rectangles into @a region for use
 *        during interactive outline resize.
 *
 * @param region Region to extend; ignored if NULL.
 */
void
Decorator::_GetOutlineFootprint(BRegion* region)
{
	if (region == NULL)
		return;

	region->Include(fTopOutlineBorder);
	region->Include(fLeftOutlineBorder);
	region->Include(fRightOutlineBorder);
	region->Include(fBottomOutlineBorder);
}


/**
 * @brief Marks the cached footprint as stale so it is recomputed on next use.
 */
void
Decorator::_InvalidateFootprint()
{
	fFootprintValid = false;
}


/**
 * @brief Drops every cached close, minimize, and zoom button bitmap so they
 *        are re-rendered with the current colors and font.
 */
void
Decorator::_InvalidateBitmaps()
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = static_cast<Decorator::Tab*>(_TabAt(i));
		for (int32 index = 0; index < 4; index++) {
			tab->closeBitmaps[index] = NULL;
			tab->minimizeBitmaps[index] = NULL;
			tab->zoomBitmaps[index] = NULL;
		}
	}
}
