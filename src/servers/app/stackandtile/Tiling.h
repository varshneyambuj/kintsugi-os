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
 * MIT License. Copyright 2010, Haiku.
 * Original authors: Clemens Zeidler.
 */

/** @file Tiling.h
    @brief Edge-tiling snapping behaviour that fits a window into a free area
           between existing tab crossings. */

#ifndef TILING_H
#define TILING_H

#include "ObjectList.h"

#include "Decorator.h"
#include "StackAndTile.h"
#include "SATGroup.h"


class SATWindow;


/**
 * @brief Snapping behaviour that aligns a window edge-to-edge inside a group.
 *
 * Searches the constraint grid of an existing SATGroup for a free rectangular
 * region whose corners match the dragged window's frame within a fixed
 * tolerance, highlights the neighbours that would border the new tile, and on
 * drop inserts the window into that area so it shares tabs with its
 * neighbours.
 */
class SATTiling : public SATSnappingBehaviour {
public:
							SATTiling(SATWindow* window);
							~SATTiling();

		bool				FindSnappingCandidates(SATGroup* group);
		bool				JoinCandidates();

		void				WindowLookChanged(window_look look);
private:
		bool				_IsTileableWindow(Window* window);

		bool				_FindFreeAreaInGroup(SATGroup* group);
		bool				_FindFreeAreaInGroup(SATGroup* group,
								Corner::position_t corner);

		bool				_InteresstingCrossing(Crossing* crossing,
								Corner::position_t corner, BRect& windowFrame);
		bool				_FindFreeArea(SATGroup* group,
								const Crossing* crossing,
								Corner::position_t areaCorner,
								BRect& windowFrame);
		bool				_HasOverlapp(SATGroup* group);
		bool				_CheckArea(SATGroup* group,
								Corner::position_t corner, BRect& windowFrame,
								float& error);
		bool				_CheckMinFreeAreaSize();
		float				_FreeAreaError(BRect& windowFrame);
		bool				_IsCornerInFreeArea(Corner::position_t corner,
								BRect& windowFrame);

		BRect				_FreeAreaSize();

		void				_HighlightWindows(SATGroup* group,
								bool highlight = true);
		bool				_SearchHighlightWindow(Tab* tab, Tab* firstOrthTab,
								Tab* secondOrthTab, const TabList* orthTabs,
								Corner::position_t areaCorner,
								Decorator::Region region, bool highlight);
		void				_HighlightWindows(WindowArea* area,
								Decorator::Region region, bool highlight);

		void				_ResetSearchResults();

		SATWindow*			fSATWindow;

		SATGroup*			fFreeAreaGroup;
		Tab*				fFreeAreaLeft;
		Tab*				fFreeAreaRight;
		Tab*				fFreeAreaTop;
		Tab*				fFreeAreaBottom;
};

#endif
