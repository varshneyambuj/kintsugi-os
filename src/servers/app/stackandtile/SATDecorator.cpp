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
 *   Copyright 2010-2015, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file SATDecorator.cpp
 * @brief Decorator and window-behaviour overrides used by Stack and Tile.
 *
 * SATDecorator extends DefaultDecorator with an extra HIGHLIGHT_STACK_AND_TILE
 * highlight state and the colour palette used to paint candidate tabs and
 * borders during a S&T drag. SATWindowBehaviour augments the default move
 * handler with magnetic snapping against the bounding rectangle of every
 * window in the dragged window's group, so a tiled cluster is treated as a
 * single object near the screen edges.
 *
 * @see DefaultDecorator, MagneticBorder, StackAndTile
 */


#include "SATDecorator.h"

#include <new>

#include <GradientLinear.h>
#include <WindowPrivate.h>

#include "DrawingEngine.h"
#include "SATWindow.h"


//#define DEBUG_SATDECORATOR
#ifdef DEBUG_SATDECORATOR
#	define STRACE(x) debug_printf x
#else
#	define STRACE(x) ;
#endif


/** @brief Static palette used to paint the four-step decorator frame in normal mode. */
static const rgb_color kFrameColors[4] = {
	{ 152, 152, 152, 255 },
	{ 240, 240, 240, 255 },
	{ 152, 152, 152, 255 },
	{ 108, 108, 108, 255 }
};

/** @brief Six-step palette used to paint the highlighted frame in S&T mode. */
static const rgb_color kHighlightFrameColors[6] = {
	{ 52, 52, 52, 255 },
	{ 140, 140, 140, 255 },
	{ 124, 124, 124, 255 },
	{ 108, 108, 108, 255 },
	{ 52, 52, 52, 255 },
	{ 8, 8, 8, 255 }
};


/**
 * @brief Constructs a SATDecorator and forwards to the DefaultDecorator base.
 *
 * @param settings Desktop settings supplying fonts, colour preferences, etc.
 * @param frame    Initial outer frame for the decorator.
 * @param desktop  Desktop the decorated window belongs to (may be NULL during
 *                 early bring-up).
 */
SATDecorator::SATDecorator(DesktopSettings& settings, BRect frame,
							Desktop* desktop)
	:
	DefaultDecorator(settings, frame, desktop)
{
}


/**
 * @brief Recomputes the highlight palette derived from the focus tab colour.
 *
 * Called by the base class during construction and whenever theme colours
 * change. Produces darkened tints used to draw the SAT highlight on the tab,
 * tab bevel, light edge, and shadow.
 *
 * @param settings Current desktop settings supplying base colours.
 */
void
SATDecorator::UpdateColors(DesktopSettings& settings)
{
	DefaultDecorator::UpdateColors(settings);

	// Called during construction, and during any changes
	fHighlightTabColor		= tint_color(fFocusTabColor, B_DARKEN_2_TINT);
	fHighlightTabColorLight	= tint_color(fHighlightTabColor,
								(B_LIGHTEN_MAX_TINT + B_LIGHTEN_2_TINT) / 2);
	fHighlightTabColorBevel	= tint_color(fHighlightTabColor, B_LIGHTEN_2_TINT);
	fHighlightTabColorShadow= tint_color(fHighlightTabColor,
								(B_DARKEN_1_TINT + B_NO_TINT) / 2);
}


/**
 * @brief Returns the colour set for one decorator component.
 *
 * Delegates to DefaultDecorator first and then, if the highlight state is
 * HIGHLIGHT_STACK_AND_TILE, overrides the relevant entries of @a _colors with
 * the SAT palette so the tab, close/zoom buttons, and frame borders draw in
 * the highlight style.
 *
 * @param component The decorator component being painted (tab, button,
 *                  border, ...).
 * @param highlight The active highlight state for this component.
 * @param _colors   Output array filled with colours for the component.
 * @param _tab      Optional Decorator::Tab whose isHighlighted flag suppresses
 *                  per-tab overrides when false; may be NULL.
 */
void
SATDecorator::GetComponentColors(Component component, uint8 highlight,
	ComponentColors _colors, Decorator::Tab* _tab)
{
	DefaultDecorator::Tab* tab = static_cast<DefaultDecorator::Tab*>(_tab);

	// Get the standard colors from the DefaultDecorator
	DefaultDecorator::GetComponentColors(component, highlight, _colors, tab);

	// Now we need to make some changes if the Stack and tile highlight is used
	if (highlight != HIGHLIGHT_STACK_AND_TILE)
		return;

	if (tab && tab->isHighlighted == false
		&& (component == COMPONENT_TAB || component == COMPONENT_CLOSE_BUTTON
			|| component == COMPONENT_ZOOM_BUTTON)) {
		return;
	}

	switch (component) {
		case COMPONENT_TAB:
			_colors[COLOR_TAB_FRAME_LIGHT] = kFrameColors[0];
			_colors[COLOR_TAB_FRAME_DARK] = kFrameColors[3];
			_colors[COLOR_TAB] = fHighlightTabColor;
			_colors[COLOR_TAB_LIGHT] = fHighlightTabColorLight;
			_colors[COLOR_TAB_BEVEL] = fHighlightTabColorBevel;
			_colors[COLOR_TAB_SHADOW] = fHighlightTabColorShadow;
			_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			break;

		case COMPONENT_CLOSE_BUTTON:
		case COMPONENT_ZOOM_BUTTON:
			_colors[COLOR_BUTTON] = fHighlightTabColor;
			_colors[COLOR_BUTTON_LIGHT] = fHighlightTabColorLight;
			break;

		case COMPONENT_LEFT_BORDER:
		case COMPONENT_RIGHT_BORDER:
		case COMPONENT_TOP_BORDER:
		case COMPONENT_BOTTOM_BORDER:
		case COMPONENT_RESIZE_CORNER:
		default:
			_colors[0] = kHighlightFrameColors[0];
			_colors[1] = kHighlightFrameColors[1];
			_colors[2] = kHighlightFrameColors[2];
			_colors[3] = kHighlightFrameColors[3];
			_colors[4] = kHighlightFrameColors[4];
			_colors[5] = kHighlightFrameColors[5];
			break;
	}
}


/**
 * @brief Constructs a window behaviour aware of its Stack and Tile listener.
 *
 * @param window The server-side window this behaviour drives.
 * @param sat    The active StackAndTile listener; used to look up the moving
 *               window's group during drag-snap calculations.
 */
SATWindowBehaviour::SATWindowBehaviour(Window* window, StackAndTile* sat)
	:
	DefaultWindowBehaviour(window),

	fStackAndTile(sat)
{
}


/**
 * @brief Adjusts a drag delta so the window also snaps to its SAT group.
 *
 * First runs the default magnetic-border logic. If that did not consume the
 * delta and the window is part of a multi-window SATGroup, runs the magnetic
 * snap a second time against the bounding rectangle of the entire group so
 * the cluster snaps as one object.
 *
 * @param window The window currently being moved.
 * @param delta  In/out drag delta in screen coordinates; may be modified to
 *               apply the snap.
 * @param now    Timestamp of the current input event used by the magnetic
 *               heuristic.
 * @return true if a snap was applied (delta has been modified), false to keep
 *         the delta as supplied.
 */
bool
SATWindowBehaviour::AlterDeltaForSnap(Window* window, BPoint& delta,
	bigtime_t now)
{
	if (DefaultWindowBehaviour::AlterDeltaForSnap(window, delta, now) == true)
		return true;

	SATWindow* satWindow = fStackAndTile->GetSATWindow(window);
	if (satWindow == NULL)
		return false;
	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return false;

	BRect groupFrame = group->WindowAt(0)->CompleteWindowFrame();
	for (int32 i = 1; i < group->CountItems(); i++)
		groupFrame = groupFrame | group->WindowAt(i)->CompleteWindowFrame();

	return fMagneticBorder.AlterDeltaForSnap(window->Screen(),
		groupFrame, delta, now);
}
