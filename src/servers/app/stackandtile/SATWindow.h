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
 * MIT License. Copyright 2010-2014, Haiku.
 * Original authors: John Scipione, Clemens Zeidler.
 */

/** @file SATWindow.h
    @brief Wrapper around a server Window that holds the per-window Stack and
           Tile state (group membership, snapping behaviours, size limits). */

#ifndef SAT_WINDOW_H
#define SAT_WINDOW_H


#include <Region.h>

#include "SATDecorator.h"
#include "SATGroup.h"
#include "Stacking.h"
#include "Tiling.h"


class Desktop;
class SATGroup;
class StackAndTile;
class Window;


/**
 * @brief Per-window state for Stack and Tile.
 *
 * Pairs a server Window with the SATGroup/WindowArea it currently belongs to,
 * caches the user-supplied size limits before SAT inflated them with decorator
 * insets, and owns the SATStacking and SATTiling snapping behaviours that
 * probe other groups during a drag.
 */
class SATWindow {
public:
								SATWindow(StackAndTile* sat, Window* window);
								~SATWindow();

			/** @brief Returns the wrapped server-side Window. */
			Window*				GetWindow() { return fWindow; }
			SATDecorator*		GetDecorator() const;
			/** @brief Returns the owning StackAndTile listener. */
			StackAndTile*		GetStackAndTile() { return fStackAndTile; }
			/** @brief Returns the desktop the wrapped window lives on. */
			Desktop*			GetDesktop() { return fDesktop; }
			//! Can be NULL if memory allocation failed!
			SATGroup*			GetGroup();
			/** @brief Returns the WindowArea this window currently occupies. */
			WindowArea*			GetWindowArea() { return fWindowArea; }

			bool				HandleMessage(SATWindow* sender,
									BPrivate::LinkReceiver& link,
									BPrivate::LinkSender& reply);

			bool				PropagateToGroup(SATGroup* group);

			// hook function called from SATGroup
			bool				AddedToGroup(SATGroup* group, WindowArea* area);
			bool				RemovedFromGroup(SATGroup* group,
									bool stayBelowMouse);
			void				RemovedFromArea(WindowArea* area);
			void				WindowLookChanged(window_look look);

			bool				StackWindow(SATWindow* child);

			void				FindSnappingCandidates();
			bool				JoinCandidates();
			void				DoGroupLayout();

			void				AdjustSizeLimits(BRect targetFrame);
			void				SetOriginalSizeLimits(int32 minWidth,
									int32 maxWidth, int32 minHeight,
									int32 maxHeight);
			void				GetSizeLimits(int32* minWidth, int32* maxWidth,
									int32* minHeight, int32* maxHeight) const;
			void				AddDecorator(int32* minWidth, int32* maxWidth,
									int32* minHeight, int32* maxHeight);
			void				AddDecorator(BRect& frame);

			// hook called when window has been resized form the outside
			void				Resized();
			bool				IsHResizeable() const;
			bool				IsVResizeable() const;

			//! @return the complete window frame including the Decorator
			BRect				CompleteWindowFrame();

			//! @return true if window is in a group with a least another window
			bool				PositionManagedBySAT();

			bool				HighlightTab(bool active);
			bool				HighlightBorders(Decorator::Region region,
									bool active);
			bool				IsTabHighlighted();
			bool				IsBordersHighlighted();

			/** @brief Returns the persistent identifier used to restore groups. */
			uint64				Id();

			bool				SetSettings(const BMessage& message);
			void				GetSettings(BMessage& message);
private:
			uint64				_GenerateId();

			void				_UpdateSizeLimits();
			void				_RestoreOriginalSize(
									bool stayBelowMouse = true);

			Window*				fWindow;
			StackAndTile*		fStackAndTile;
			Desktop*			fDesktop;

			//! Current group.
			WindowArea*			fWindowArea;

			SATSnappingBehaviour*	fOngoingSnapping;
			SATStacking			fSATStacking;
			SATTiling			fSATTiling;

			SATSnappingBehaviourList	fSATSnappingBehaviourList;

			int32				fOriginalMinWidth;
			int32				fOriginalMaxWidth;
			int32				fOriginalMinHeight;
			int32				fOriginalMaxHeight;

			float				fOriginalWidth;
			float				fOriginalHeight;

			uint64				fId;

			float				fOldTabLocatiom;
};


#endif
