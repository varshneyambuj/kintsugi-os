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

/** @file SATGroup.h
    @brief Constraint-based grouping of Stack and Tile windows.

    Defines the Tab/Crossing/Corner grid that tiled windows snap to, the
    WindowArea that one or more windows occupy in the grid, and the SATGroup
    that ties areas together via a LinearProgramming::LinearSpec solver. */

#ifndef SAT_GROUP_H
#define SAT_GROUP_H


#include <Rect.h>

#include <AutoDeleter.h>
#include "ObjectList.h"
#include "Referenceable.h"

#include "MagneticBorder.h"

#include "LinearSpec.h"


class SATWindow;
class Tab;
class WindowArea;

typedef BObjectList<SATWindow> SATWindowList;


/**
 * @brief One of the four corners of a tab Crossing.
 *
 * Records whether the corner is occupied by a WindowArea (kUsed), available
 * for a new neighbour (kFree), or geometrically unreachable (kNotDockable),
 * and points back to the WindowArea that owns it when used.
 */
class Corner {
public:
		enum info_t
		{
			kFree,
			kUsed,
			kNotDockable
		};

		enum position_t
		{
			kLeftTop = 0,
			kRightTop = 1,
			kLeftBottom = 2,
			kRightBottom = 3
		};

						Corner();
		void			Trace() const;

		info_t			status;
		WindowArea*		windowArea;
};


/**
 * @brief Intersection of a vertical and a horizontal Tab.
 *
 * Each Crossing carries the four Corners that meet at this point and acts as
 * the glue between adjacent WindowAreas: an area's four bounding corners are
 * actually four Corner slots in four distinct Crossings.
 */
class Crossing : public BReferenceable {
public:
								Crossing(Tab* vertical, Tab* horizontal);
								~Crossing();

			Corner*				GetCorner(Corner::position_t corner) const;
			Corner*				GetOppositeCorner(
									Corner::position_t corner) const;

			/** @brief Returns the Corner located on the upper-left of this Crossing. */
			Corner*				LeftTopCorner()
									{ return &fCorners[Corner::kLeftTop]; }
			/** @brief Returns the Corner located on the upper-right of this Crossing. */
			Corner*				RightTopCorner()
									{ return &fCorners[Corner::kRightTop]; }
			/** @brief Returns the Corner located on the lower-left of this Crossing. */
			Corner*				LeftBottomCorner()
									{ return &fCorners[Corner::kLeftBottom]; }
			/** @brief Returns the Corner located on the lower-right of this Crossing. */
			Corner*				RightBottomCorner()
									{ return &fCorners[Corner::kRightBottom]; }

			Tab*				VerticalTab() const;
			Tab*				HorizontalTab() const;

			void				Trace() const;
private:
			Corner				fCorners[4];

			BReference<Tab>		fVerticalTab;
			BReference<Tab>		fHorizontalTab;
};


typedef BObjectList<Constraint> ConstraintList;
class SATGroup;

typedef BObjectList<Crossing> CrossingList;


/** @brief Bias added to every coordinate so the LP solver only sees positive values. */
const float kMakePositiveOffset = 5000;


/**
 * @brief A horizontal or vertical guideline shared by adjacent windows.
 *
 * A Tab wraps one solver Variable whose value is the tab's screen coordinate
 * (offset by kMakePositiveOffset). Tabs intersect each other through
 * Crossings, and adjacent WindowAreas pin themselves to the same Tab so the
 * solver keeps their shared edge aligned.
 */
class Tab : public BReferenceable {
public:
		enum orientation_t
		{
			kVertical,
			kHorizontal
		};

								Tab(SATGroup* group, Variable* variable,
									orientation_t orientation);
								~Tab();

			float				Position() const;
			void				SetPosition(float position);
			orientation_t		Orientation() const;
			/** @brief Returns the LP variable that backs this Tab. */
			Variable*			Var() {	return fVariable.Get(); }

			//! Caller takes ownership of the constraint.
			Constraint*			Connect(Variable* variable);

			BReference<Crossing>	AddCrossing(Tab* tab);
			bool				RemoveCrossing(Crossing* crossing);
			int32				FindCrossingIndex(Tab* tab);
			int32				FindCrossingIndex(float tabPosition);
			Crossing*			FindCrossing(Tab* tab);
			Crossing*			FindCrossing(float tabPosition);

			const CrossingList*	GetCrossingList() const;

	static	int					CompareFunction(const Tab* tab1,
									const Tab* tab2);

private:
			SATGroup*			fGroup;
			ObjectDeleter<Variable>
								fVariable;
			orientation_t		fOrientation;

			CrossingList		fCrossingList;
};


/**
 * @brief Rectangular region of the tab grid occupied by one or more windows.
 *
 * The four bounding Crossings define the area; the SATWindowList contains the
 * stacked windows that share it (multiple windows in one area means they are
 * tab-stacked). The class also owns the size-related solver constraints that
 * keep the area within the windows' min/max dimensions.
 */
class WindowArea : public BReferenceable {
public:
								WindowArea(Crossing* leftTop,
									Crossing* rightTop, Crossing* leftBottom,
									Crossing* rightBottom);
								~WindowArea();

			bool				Init(SATGroup* group);
			/** @brief Returns the SATGroup this area belongs to. */
			SATGroup*			Group() { return fGroup; }

			void				DoGroupLayout();
			void				UpdateSizeLimits();
			void				UpdateSizeConstaints(const BRect& frame);

	const	SATWindowList&		WindowList() { return fWindowList; }
	const	SATWindowList&		LayerOrder() { return fWindowLayerOrder; }
			bool				MoveWindowToPosition(SATWindow* window,
									int32 index);
			SATWindow*			TopWindow();

			/** @brief Returns the Crossing at the area's upper-left corner. */
			Crossing*			LeftTopCrossing()
									{ return fLeftTopCrossing.Get(); }
			/** @brief Returns the Crossing at the area's upper-right corner. */
			Crossing*			RightTopCrossing()
									{ return fRightTopCrossing.Get(); }
			/** @brief Returns the Crossing at the area's lower-left corner. */
			Crossing*			LeftBottomCrossing()
									{ return fLeftBottomCrossing.Get(); }
			/** @brief Returns the Crossing at the area's lower-right corner. */
			Crossing*			RightBottomCrossing()
									{ return fRightBottomCrossing.Get(); }

			Tab*				LeftTab();
			Tab*				RightTab();
			Tab*				TopTab();
			Tab*				BottomTab();

			/** @brief Returns the LP variable for the area's left edge. */
			Variable*			LeftVar() { return LeftTab()->Var(); }
			/** @brief Returns the LP variable for the area's right edge. */
			Variable*			RightVar() { return RightTab()->Var(); }
			/** @brief Returns the LP variable for the area's top edge. */
			Variable*			TopVar() { return TopTab()->Var(); }
			/** @brief Returns the LP variable for the area's bottom edge. */
			Variable*			BottomVar() { return BottomTab()->Var(); }

			BRect				Frame();

			bool				PropagateToGroup(SATGroup* group);

			bool				MoveToTopLayer(SATWindow* window);

private:
		friend class SATGroup;
			void				_UninitConstraints();
			void				_UpdateConstraintValues();

			/*! SATGroup adds new windows to the area. */
			bool				_AddWindow(SATWindow* window,
									SATWindow* after = NULL);
			/*! After the last window has been removed the WindowArea delete
			himself and clean up all crossings. */
			bool				_RemoveWindow(SATWindow* window);

	inline	void				_InitCorners();
	inline	void				_CleanupCorners();
	inline	void				_SetToWindowCorner(Corner* corner);
	inline	void				_SetToNeighbourCorner(Corner* neighbour);
	inline	void				_UnsetWindowCorner(Corner* corner);
		//! opponent is the other neighbour of the neighbour
	inline	void				_UnsetNeighbourCorner(Corner* neighbour,
									Corner* opponent);

			// Find crossing by tab position in group and if not exist create
			// it.
			BReference<Crossing>	_CrossingByPosition(Crossing* crossing,
										SATGroup* group);

			void				_MoveToSAT(SATWindow* topWindow);

			BReference<SATGroup>	fGroup;

			SATWindowList		fWindowList;

			SATWindowList		fWindowLayerOrder;

			BReference<Crossing>	fLeftTopCrossing;
			BReference<Crossing>	fRightTopCrossing;
			BReference<Crossing>	fLeftBottomCrossing;
			BReference<Crossing>	fRightBottomCrossing;

			Constraint*			fMinWidthConstraint;
			Constraint*			fMinHeightConstraint;
			Constraint*			fMaxWidthConstraint;
			Constraint*			fMaxHeightConstraint;
			Constraint*			fWidthConstraint;
			Constraint*			fHeightConstraint;

			MagneticBorder		fMagneticBorder;
};


typedef BObjectList<WindowArea> WindowAreaList;
typedef BObjectList<Tab> TabList;

class BMessage;
class StackAndTile;


/**
 * @brief A connected cluster of windows that move and resize together.
 *
 * Owns the LinearSpec solver, the lists of horizontal and vertical Tabs, and
 * the WindowAreas that partition the group's bounding rectangle. Splits
 * itself when removing a window disconnects the cluster, and can serialize /
 * restore its layout via BMessage so groups survive across sessions.
 */
class SATGroup : public BReferenceable {
public:
		friend class Tab;
		friend class WindowArea;
		friend class GroupCookie;

								SATGroup();
								~SATGroup();

			/** @brief Returns the LP solver shared by every Tab and area in the group. */
			LinearSpec*			GetLinearSpec() { return fLinearSpec; }

			/*! Create a new WindowArea from the crossing and add the window. */
			bool				AddWindow(SATWindow* window, Tab* left,
									Tab* top, Tab* right, Tab* bottom);
			/*! Add a window to an existing window area. */
			bool				AddWindow(SATWindow* window, WindowArea* area,
									SATWindow* after = NULL);
			/*! If stayBelowMouse is true move the removed window below the
			cursor if necessary. */
			bool				RemoveWindow(SATWindow* window,
									bool stayBelowMouse = true);
			int32				CountItems();
			SATWindow*			WindowAt(int32 index);

			SATWindow*			ActiveWindow() const;
			void				SetActiveWindow(SATWindow* window);

			/** @brief Returns the list of WindowAreas that tile this group. */
			const WindowAreaList&	GetAreaList() { return fWindowAreaList; }

			/*! @return a sorted tab list. */
			const TabList*		HorizontalTabs();
			const TabList*		VerticalTabs();

			Tab*				FindHorizontalTab(float position);
			Tab*				FindVerticalTab(float position);

			void				WindowAreaRemoved(WindowArea* area);

	static	status_t			RestoreGroup(const BMessage& archive,
									StackAndTile* sat);
			status_t			ArchiveGroup(BMessage& archive);

private:
			BReference<Tab>		_AddHorizontalTab(float position = 0);
			BReference<Tab>		_AddVerticalTab(float position = 0);

			bool				_RemoveHorizontalTab(Tab* tab);
			bool				_RemoveVerticalTab(Tab* tab);

			Tab*				_FindTab(const TabList& list, float position);

			void				_SplitGroupIfNecessary(
									WindowArea* removedArea);
			void				_FillNeighbourList(
									WindowAreaList& neighbourWindows,
									WindowArea* area);
			void				_LeftNeighbours(
									WindowAreaList& neighbourWindows,
									WindowArea* window);
			void				_TopNeighbours(
									WindowAreaList& neighbourWindows,
									WindowArea* window);
			void				_RightNeighbours(
									WindowAreaList& neighbourWindows,
									WindowArea* window);
			void				_BottomNeighbours(
									WindowAreaList& neighbourWindows,
									WindowArea* window);
			bool				_FindConnectedGroup(WindowAreaList& seedList,
									WindowArea* removedArea,
									WindowAreaList& newGroup);
			void				_FollowSeed(WindowArea* area, WindowArea* veto,
									WindowAreaList& seedList,
									WindowAreaList& newGroup);
			void				_SpawnNewGroup(const WindowAreaList& newGroup);

			void				_EnsureGroupIsOnScreen(SATGroup* group);
	inline	void				_CallculateXOffset(BPoint& offset, BRect& frame,
									BRect& screen);
	inline	void				_CallculateYOffset(BPoint& offset, BRect& frame,
									BRect& screen);

protected:
			WindowAreaList		fWindowAreaList;
			SATWindowList		fSATWindowList;

			LinearSpec*			fLinearSpec;

private:
			TabList				fHorizontalTabs;
			bool				fHorizontalTabsSorted;
			TabList				fVerticalTabs;
			bool				fVerticalTabsSorted;

			SATWindow*			fActiveWindow;
};


typedef BObjectList<SATGroup> SATGroupList;

#endif
