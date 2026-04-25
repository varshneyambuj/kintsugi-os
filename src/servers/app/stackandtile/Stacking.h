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

/** @file Stacking.h
    @brief Tab-stacking gesture handler and per-window stacking behaviour for
           Stack and Tile. */

#ifndef STACKING_H
#define STACKING_H

#include "ObjectList.h"
#include "StackAndTile.h"


class SATWindow;


/**
 * @brief Dispatches client-side stacking protocol messages to a SATWindow.
 *
 * Decodes the message opcode and parameters from a LinkReceiver, performs the
 * requested operation on the sender's stack (add, remove, count, query), and
 * writes a status reply back through the LinkSender.
 */
class StackingEventHandler
{
public:
	static bool				HandleMessage(SATWindow* sender,
								BPrivate::LinkReceiver& link,
								BPrivate::LinkSender& reply);
};


/**
 * @brief Snapping behaviour that joins a window to an existing stack of tabs.
 *
 * Detects whether the user is dragging a window's tab onto another window's
 * tab strip and, if so, treats the target as the stacking parent so that on
 * drop the dragged window becomes a new tab inside the parent's WindowStack.
 */
class SATStacking : public SATSnappingBehaviour {
public:
							SATStacking(SATWindow* window);
							~SATStacking();

		bool				FindSnappingCandidates(SATGroup* group);
		bool				JoinCandidates();
		void				DoWindowLayout();

		void				RemovedFromArea(WindowArea* area);
		void				WindowLookChanged(window_look look);
private:
		bool				_IsStackableWindow(Window* window);
		void				_ClearSearchResult();
		void				_HighlightWindows(bool highlight = true);

		SATWindow*			fSATWindow;

		SATWindow*			fStackingParent;
};

#endif
