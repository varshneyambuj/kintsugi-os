/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2010-2015, Haiku.
 * Original authors: Clemens Zeidler.
 */

/** @file SATDecorator.h
    @brief Default decorator subclass and window behaviour adapter that add
           visual feedback and group-aware snapping for Stack and Tile. */

#ifndef SAT_DECORATOR_H
#define SAT_DECORATOR_H


#include "DecorManager.h"
#include "DefaultDecorator.h"
#include "DefaultWindowBehaviour.h"
#include "StackAndTile.h"


class Desktop;


/**
 * @brief Decorator that recolours frame and tab while in S&T snapping mode.
 *
 * Layers a custom highlight palette on top of the DefaultDecorator so that
 * candidate stacking partners and tiling targets are visibly marked while the
 * user holds the Stack and Tile modifier and drags a window. All non-S&T
 * highlights and behaviours are inherited from DefaultDecorator unchanged.
 */
class SATDecorator : public DefaultDecorator {
public:
			enum {
				HIGHLIGHT_STACK_AND_TILE = HIGHLIGHT_USER_DEFINED
			};

public:
								SATDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);

protected:
	virtual	void				UpdateColors(DesktopSettings& settings);
	virtual	void				GetComponentColors(Component component,
									uint8 highlight, ComponentColors _colors,
									Decorator::Tab* tab = NULL);

private:
				rgb_color		fHighlightTabColor;
				rgb_color		fHighlightTabColorLight;
				rgb_color		fHighlightTabColorBevel;
				rgb_color		fHighlightTabColorShadow;
};


/**
 * @brief DefaultWindowBehaviour augmented with cross-group magnetic snapping.
 *
 * Lets the magnetic-border drag logic snap the moving window not only to the
 * window's own frame but also to the bounding rectangle of every other window
 * in the same SAT group, so a tiled cluster behaves like a single object near
 * screen edges.
 */
class SATWindowBehaviour : public DefaultWindowBehaviour {
public:
								SATWindowBehaviour(Window* window,
									StackAndTile* sat);

protected:
	virtual bool				AlterDeltaForSnap(Window* window, BPoint& delta,
									bigtime_t now);

private:
			StackAndTile*		fStackAndTile;
};


#endif
