/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2005-2008, Haiku Inc.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file WorkspacesView.h
 *  @brief View that renders an interactive miniature overview of all workspaces. */

#ifndef WORKSPACES_VIEW_H
#define WORKSPACES_VIEW_H


#include "View.h"


/** @brief Draws a thumbnail grid of workspaces and handles workspace-switch clicks. */
class WorkspacesView : public View {
public:
	/** @brief Constructs a WorkspacesView with the given geometry.
	 *  @param frame          Frame in parent coordinate space.
	 *  @param scrollingOffset Scrolling offset for the local coordinate system.
	 *  @param name           View name.
	 *  @param token          Unique view token.
	 *  @param resize         Resize mode flags.
	 *  @param flags          View flags. */
					WorkspacesView(BRect frame, BPoint scrollingOffset,
						const char* name, int32 token, uint32 resize,
						uint32 flags);
	virtual			~WorkspacesView();

	/** @brief Called when this view is attached to a Window; registers for workspace notifications.
	 *  @param window The Window this view is now part of. */
	virtual	void	AttachedToWindow(::Window* window);

	/** @brief Called when this view is detached from its Window; unregisters notifications. */
	virtual	void	DetachedFromWindow();

	/** @brief Renders all workspace thumbnails into the given drawing engine.
	 *  @param drawingEngine          Engine to draw with.
	 *  @param effectiveClipping      Combined clipping region.
	 *  @param windowContentClipping  Window content clipping region.
	 *  @param deep                   If true, also draw child views. */
	virtual	void	Draw(DrawingEngine* drawingEngine,
						const BRegion* effectiveClipping,
						const BRegion* windowContentClipping, bool deep = false);

	/** @brief Handles a mouse-down event to start workspace switching or window dragging.
	 *  @param message Message carrying event details.
	 *  @param where   Click position in view coordinates. */
	virtual	void	MouseDown(BMessage* message, BPoint where);

	/** @brief Handles a mouse-up event to complete a workspace switch or window move.
	 *  @param message Message carrying event details.
	 *  @param where   Release position in view coordinates. */
	virtual	void	MouseUp(BMessage* message, BPoint where);

	/** @brief Handles a mouse-moved event to provide live drag feedback.
	 *  @param message Message carrying event details.
	 *  @param where   Current cursor position in view coordinates. */
	virtual	void	MouseMoved(BMessage* message, BPoint where);

	/** @brief Notifies the view that a window's properties have changed (size, title, etc.).
	 *  @param window The Window that changed. */
			void	WindowChanged(::Window* window);

	/** @brief Notifies the view that a window has been removed from the system.
	 *  @param window The Window that was removed. */
			void	WindowRemoved(::Window* window);

private:
	/** @brief Computes the grid dimensions of the workspace overview.
	 *  @param columns Receives the number of columns.
	 *  @param rows    Receives the number of rows. */
			void	_GetGrid(int32& columns, int32& rows);

	/** @brief Returns the screen frame for the workspace at the given index.
	 *  @param index Zero-based workspace index.
	 *  @return Screen frame in view coordinates. */
			BRect	_ScreenFrame(int32 index);

	/** @brief Returns the thumbnail frame for the workspace at the given index.
	 *  @param index Zero-based workspace index.
	 *  @return Thumbnail BRect in view coordinates. */
			BRect	_WorkspaceAt(int32 index);

	/** @brief Returns the thumbnail frame for the workspace under the given point.
	 *  @param where Point in view coordinates.
	 *  @param index Receives the zero-based index of the workspace hit.
	 *  @return Thumbnail BRect, or an empty rect if no workspace was hit. */
			BRect	_WorkspaceAt(BPoint where, int32& index);

	/** @brief Computes the scaled thumbnail frame for a window within a workspace thumbnail.
	 *  @param workspaceFrame Thumbnail frame of the workspace.
	 *  @param screenFrame    Full screen frame of that workspace.
	 *  @param windowFrame    Full-size frame of the window.
	 *  @param windowPosition Saved position of the window in that workspace.
	 *  @return Scaled window frame within the thumbnail. */
			BRect	_WindowFrame(const BRect& workspaceFrame,
						const BRect& screenFrame, const BRect& windowFrame,
						BPoint windowPosition);

	/** @brief Draws a single window thumbnail into the workspace miniature.
	 *  @param drawingEngine      Engine to draw with.
	 *  @param workspaceFrame     Thumbnail frame of the workspace.
	 *  @param screenFrame        Full screen frame.
	 *  @param window             Window to draw.
	 *  @param windowPosition     Saved window position.
	 *  @param backgroundRegion   Region remaining for background fill after this window.
	 *  @param workspaceActive    true if the workspace is the currently active one. */
			void	_DrawWindow(DrawingEngine* drawingEngine,
						const BRect& workspaceFrame, const BRect& screenFrame,
						::Window* window, BPoint windowPosition,
						BRegion& backgroundRegion, bool workspaceActive);

	/** @brief Draws a complete workspace thumbnail including its windows.
	 *  @param drawingEngine Engine to draw with.
	 *  @param redraw        Region that must be redrawn.
	 *  @param index         Zero-based workspace index. */
			void	_DrawWorkspace(DrawingEngine* drawingEngine,
						BRegion& redraw, int32 index);

	/** @brief Darkens a color by a fixed amount for border/shadow rendering.
	 *  @param color Color to darken in place. */
			void	_DarkenColor(rgb_color& color) const;

	/** @brief Invalidates the entire view, scheduling a full repaint. */
			void	_Invalidate() const;

private:
	::Window*		fSelectedWindow;    /**< Window currently selected for dragging. */
	int32			fSelectedWorkspace; /**< Index of the workspace targeted by the current interaction. */
	bool			fHasMoved;          /**< true if the mouse has moved since the last button-down. */
	BPoint			fClickPoint;        /**< View-local position of the most recent mouse-down. */
	BPoint			fLeftTopOffset;     /**< Offset from the window's top-left to the click point. */
};

#endif	// WORKSPACES_VIEW_H
