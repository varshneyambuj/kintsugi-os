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
 * MIT License. Copyright 2001-2020, Haiku.
 * Original authors: Adrian Oanca, Stephan Aßmus, Axel Dörfler,
 *                   Andrej Spielmann, Brecht Machiels, Clemens Zeidler,
 *                   Joseph Groover, Tri-Edge AI, Jacob Secunda.
 */

/** @file Desktop.h
    @brief Core desktop manager owning windows, workspaces, screens, and input dispatch. */

#ifndef DESKTOP_H
#define DESKTOP_H


#include <AutoDeleter.h>
#include <Autolock.h>
#include <InterfaceDefs.h>
#include <List.h>
#include <Menu.h>
#include <ObjectList.h>
#include <Region.h>
#include <String.h>
#include <Window.h>

#include <ServerProtocolStructs.h>

#include "CursorManager.h"
#include "DelayedMessage.h"
#include "DesktopListener.h"
#include "DesktopSettings.h"
#include "EventDispatcher.h"
#include "MessageLooper.h"
#include "MultiLocker.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ServerCursor.h"
#include "StackAndTile.h"
#include "VirtualScreen.h"
#include "WindowList.h"
#include "Workspace.h"
#include "WorkspacePrivate.h"


class BMessage;

class DecorAddOn;
class DrawingEngine;
class HWInterface;
class ServerApp;
class Window;
class WorkspacesView;
struct server_read_only_memory;

namespace BPrivate {
	class LinkSender;
};


/** @brief Central desktop object managing windows, workspaces, input events, and rendering
           for a single user session. */
class Desktop : public DesktopObservable, public MessageLooper,
	public ScreenOwner {
public:
								Desktop(uid_t userID,
									const char* targetScreen);
	virtual						~Desktop();

			/** @brief Registers a DesktopListener to receive desktop event notifications.
			    @param listener The listener to register. */
			void				RegisterListener(DesktopListener* listener);

			/** @brief Initialises the desktop, creating screens and loading settings.
			    @return B_OK on success, or an error code. */
			status_t			Init();

			/** @brief Returns the UID of the user who owns this desktop.
			    @return User ID. */
			uid_t				UserID() const { return fUserID; }

			/** @brief Returns the name of the target screen, or NULL for the default.
			    @return Target screen name. */
			const char*			TargetScreen() { return fTargetScreen; }

	/** @brief Returns the port on which this desktop receives messages.
	    @return Message port ID. */
	virtual port_id				MessagePort() const { return fMessagePort; }

			/** @brief Returns the area_id of the shared read-only memory block.
			    @return Shared read-only area ID. */
			area_id				SharedReadOnlyArea() const
									{ return fSharedReadOnlyArea; }

			/** @brief Returns a reference to the event dispatcher for this desktop.
			    @return Reference to the EventDispatcher. */
			::EventDispatcher&	EventDispatcher() { return fEventDispatcher; }

			/** @brief Sends a message code to all registered server applications.
			    @param code Message code to broadcast. */
			void				BroadcastToAllApps(int32 code);

			/** @brief Sends a message code to all windows on this desktop.
			    @param code Message code to broadcast. */
			void				BroadcastToAllWindows(int32 code);

			/** @brief Adds all window ports as targets in a DelayedMessage.
			    @param message The DelayedMessage to populate.
			    @return Number of targets added. */
			int32				GetAllWindowTargets(DelayedMessage& message);

			/** @brief Adds all application ports as targets in a DelayedMessage.
			    @param message The DelayedMessage to populate.
			    @return Number of targets added. */
			int32				GetAllAppTargets(DelayedMessage& message);

			/** @brief Processes a key event and dispatches it to listeners.
			    @param what Key event type (e.g. B_KEY_DOWN).
			    @param key Raw key code.
			    @param modifiers Current modifier key state.
			    @return B_DISPATCH_MESSAGE or B_SKIP_MESSAGE. */
			filter_result		KeyEvent(uint32 what, int32 key,
									int32 modifiers);

	// Locking
			/** @brief Acquires a read lock allowing a single window to be accessed.
			    @return true if the lock was acquired. */
			bool				LockSingleWindow()
									{ return fWindowLock.ReadLock(); }

			/** @brief Releases the single-window read lock. */
			void				UnlockSingleWindow()
									{ fWindowLock.ReadUnlock(); }

			/** @brief Acquires a write lock giving exclusive access to all windows.
			    @return true if the lock was acquired. */
			bool				LockAllWindows()
									{ return fWindowLock.WriteLock(); }

			/** @brief Releases the all-windows write lock. */
			void				UnlockAllWindows()
									{ fWindowLock.WriteUnlock(); }

			/** @brief Returns a reference to the window multi-locker.
			    @return Const reference to the MultiLocker. */
			const MultiLocker&	WindowLocker() { return fWindowLock; }

	// Mouse and cursor methods

			/** @brief Sets the active cursor shown on this desktop.
			    @param cursor The ServerCursor to display. */
			void				SetCursor(ServerCursor* cursor);

			/** @brief Returns a reference-counted handle to the current cursor.
			    @return ServerCursorReference for the current cursor. */
			ServerCursorReference Cursor() const;

			/** @brief Sets an internal management cursor (used by decorators, etc.).
			    @param newCursor The ServerCursor to use for management. */
			void				SetManagementCursor(ServerCursor* newCursor);

			/** @brief Updates the last known mouse position, button state, and window under mouse.
			    @param position Current mouse position in screen coordinates.
			    @param buttons Current mouse button state bitmask.
			    @param windowUnderMouse The window currently under the mouse pointer. */
			void				SetLastMouseState(const BPoint& position,
									int32 buttons, Window* windowUnderMouse);
									// for use by the mouse filter only
									// both mouse position calls require
									// the Desktop object to be locked
									// already

			/** @brief Retrieves the last known mouse position and button state.
			    @param position Output current mouse position.
			    @param buttons Output current button state bitmask. */
			void				GetLastMouseState(BPoint* position,
									int32* buttons) const;
									// for use by ServerWindow

			/** @brief Returns the cursor manager for this desktop.
			    @return Reference to the CursorManager. */
			CursorManager&		GetCursorManager() { return fCursorManager; }

	// Screen and drawing related methods

			/** @brief Changes the display mode of a screen in a given workspace.
			    @param workspace Workspace index.
			    @param id Screen identifier.
			    @param mode New display mode to apply.
			    @param makeDefault true to persist the mode as the default.
			    @return B_OK on success, or an error code. */
			status_t			SetScreenMode(int32 workspace, int32 id,
									const display_mode& mode, bool makeDefault);

			/** @brief Retrieves the current display mode for a screen in a workspace.
			    @param workspace Workspace index.
			    @param id Screen identifier.
			    @param mode Output current display mode.
			    @return B_OK on success, or an error code. */
			status_t			GetScreenMode(int32 workspace, int32 id,
									display_mode& mode);

			/** @brief Retrieves the frame rectangle of a screen in a workspace.
			    @param workspace Workspace index.
			    @param id Screen identifier.
			    @param frame Output frame rectangle.
			    @return B_OK on success, or an error code. */
			status_t			GetScreenFrame(int32 workspace, int32 id,
									BRect& frame);

			/** @brief Reverts screen modes to their defaults for the given workspace bitmask.
			    @param workspaces Bitmask of workspaces to revert. */
			void				RevertScreenModes(uint32 workspaces);

			/** @brief Sets the display brightness for a screen.
			    @param id Screen identifier.
			    @param brightness Brightness level in [0.0, 1.0].
			    @return B_OK on success, or an error code. */
			status_t			SetBrightness(int32 id, float brightness);

			/** @brief Returns the screen lock used to protect screen state.
			    @return Reference to the screen MultiLocker. */
			MultiLocker&		ScreenLocker() { return fScreenLock; }

			/** @brief Locks the direct frame buffer for exclusive access by a team.
			    @param team The team ID requesting direct screen access.
			    @return B_OK on success, or B_BUSY if already locked. */
			status_t			LockDirectScreen(team_id team);

			/** @brief Releases the direct frame buffer lock held by a team.
			    @param team The team ID releasing the lock.
			    @return B_OK on success, or an error code. */
			status_t			UnlockDirectScreen(team_id team);

			/** @brief Returns the virtual screen compositing all physical screens.
			    @return Const reference to the VirtualScreen. */
			const ::VirtualScreen& VirtualScreen() const
									{ return fVirtualScreen; }

			/** @brief Returns the drawing engine used for compositing.
			    @return Pointer to the DrawingEngine. */
			DrawingEngine*		GetDrawingEngine() const
									{ return fVirtualScreen.DrawingEngine(); }

			/** @brief Returns the hardware interface object.
			    @return Pointer to the HWInterface. */
			::HWInterface*		HWInterface() const
									{ return fVirtualScreen.HWInterface(); }

			/** @brief Rebuilds clipping and redraws areas affected by a window change.
			    @param window The window that changed.
			    @param dirty The dirty region to redraw. */
			void				RebuildAndRedrawAfterWindowChange(
									Window* window, BRegion& dirty);
									// the window lock must be held when calling
									// this function

	// ScreenOwner implementation
	/** @brief Called when a screen is removed from the system. */
	virtual	void				ScreenRemoved(Screen* screen) {}
	/** @brief Called when a new screen is added to the system. */
	virtual	void				ScreenAdded(Screen* screen) {}
	/** @brief Called when a screen's configuration changes.
	    @param screen The screen that changed. */
	virtual	void				ScreenChanged(Screen* screen);
	/** @brief Called to release a screen; returns false as Desktop does not release screens.
	    @return false. */
	virtual	bool				ReleaseScreen(Screen* screen) { return false; }

	// Workspace methods

			/** @brief Switches to a workspace asynchronously (returns before the switch completes).
			    @param index Target workspace index.
			    @param moveFocusWindow true to move the focus window to the new workspace. */
			void				SetWorkspaceAsync(int32 index,
									bool moveFocusWindow = false);

			/** @brief Switches to a workspace synchronously.
			    @param index Target workspace index.
			    @param moveFocusWindow true to move the focus window to the new workspace. */
			void				SetWorkspace(int32 index,
									bool moveFocusWindow = false);

			/** @brief Returns the index of the currently active workspace.
			    @return Current workspace index. */
			int32				CurrentWorkspace()
									{ return fCurrentWorkspace; }

			/** @brief Returns the private state of a workspace by index.
			    @param index Workspace index.
			    @return Reference to the Workspace::Private state. */
			Workspace::Private&	WorkspaceAt(int32 index)
									{ return fWorkspaces[index]; }

			/** @brief Reconfigures the workspace grid layout.
			    @param columns Number of workspace columns.
			    @param rows Number of workspace rows.
			    @return B_OK on success, or an error code. */
			status_t			SetWorkspacesLayout(int32 columns, int32 rows);

			/** @brief Returns the screen-space frame of a workspace.
			    @param index Workspace index.
			    @return BRect covering the workspace. */
			BRect				WorkspaceFrame(int32 index) const;

			/** @brief Persists the current workspace configuration to settings.
			    @param index Workspace index to store. */
			void				StoreWorkspaceConfiguration(int32 index);

			/** @brief Registers a WorkspacesView so it is notified of workspace changes.
			    @param view The view to register. */
			void				AddWorkspacesView(WorkspacesView* view);

			/** @brief Unregisters a WorkspacesView from workspace change notifications.
			    @param view The view to remove. */
			void				RemoveWorkspacesView(WorkspacesView* view);

	// Window methods

			/** @brief Selects a window, bringing it to the front without activating it.
			    @param window The window to select. */
			void				SelectWindow(Window* window);

			/** @brief Activates a window, giving it keyboard focus and raising it.
			    @param window The window to activate. */
			void				ActivateWindow(Window* window);

			/** @brief Sends a window behind another in the stacking order.
			    @param window The window to move back.
			    @param behindOf The window it should go behind, or NULL for the very back.
			    @param sendStack true to move the entire window stack. */
			void				SendWindowBehind(Window* window,
									Window* behindOf = NULL,
									bool sendStack = true);

			/** @brief Makes a window visible.
			    @param window The window to show. */
			void				ShowWindow(Window* window);

			/** @brief Hides a window.
			    @param window The window to hide.
			    @param fromMinimize true if hiding due to minimisation. */
			void				HideWindow(Window* window,
									bool fromMinimize = false);

			/** @brief Minimises or restores a window.
			    @param window The window to affect.
			    @param minimize true to minimise, false to restore. */
			void				MinimizeWindow(Window* window, bool minimize);

			/** @brief Moves a window by the given delta in a workspace.
			    @param window The window to move.
			    @param x Horizontal offset in pixels.
			    @param y Vertical offset in pixels.
			    @param workspace Target workspace index, or -1 for current. */
			void				MoveWindowBy(Window* window, float x, float y,
									int32 workspace = -1);

			/** @brief Resizes a window by the given delta.
			    @param window The window to resize.
			    @param x Width delta in pixels.
			    @param y Height delta in pixels. */
			void				ResizeWindowBy(Window* window, float x,
									float y);

			/** @brief Sets the outline drag delta for a window being resized.
			    @param window The window.
			    @param delta Outline position delta. */
			void				SetWindowOutlinesDelta(Window* window,
									BPoint delta);

			/** @brief Moves the tab of a window to a new position.
			    @param window The window.
			    @param location New tab offset.
			    @param isShifting true if this is a tab-shifting operation.
			    @return true if the tab position changed. */
			bool				SetWindowTabLocation(Window* window,
									float location, bool isShifting);

			/** @brief Applies decorator-specific settings to a window.
			    @param window The window.
			    @param settings BMessage containing the decorator settings.
			    @return true if settings were applied. */
			bool				SetWindowDecoratorSettings(Window* window,
									const BMessage& settings);

			/** @brief Moves a window to a set of workspaces defined by a bitmask.
			    @param window The window to move.
			    @param workspaces Bitmask of target workspaces. */
			void				SetWindowWorkspaces(Window* window,
									uint32 workspaces);

			/** @brief Adds a window to the desktop's window list.
			    @param window The window to add. */
			void				AddWindow(Window* window);

			/** @brief Removes a window from the desktop's window list.
			    @param window The window to remove. */
			void				RemoveWindow(Window* window);

			/** @brief Adds a window to a subset window's floating set.
			    @param subset The floating subset window.
			    @param window The window to add to the subset.
			    @return true on success. */
			bool				AddWindowToSubset(Window* subset,
									Window* window);

			/** @brief Removes a window from a subset window's floating set.
			    @param subset The floating subset window.
			    @param window The window to remove. */
			void				RemoveWindowFromSubset(Window* subset,
									Window* window);

			/** @brief Notifies a window that font settings have changed.
			    @param window The window to notify. */
			void				FontsChanged(Window* window);

			/** @brief Notifies a window that a UI color has changed.
			    @param window The window to notify.
			    @param which Which UI color changed.
			    @param color The new color value. */
			void				ColorUpdated(Window* window, color_which which,
									rgb_color color);

			/** @brief Changes the visual look of a window.
			    @param window The window to update.
			    @param look New window_look value. */
			void				SetWindowLook(Window* window, window_look look);

			/** @brief Changes the behavioural feel of a window.
			    @param window The window to update.
			    @param feel New window_feel value. */
			void				SetWindowFeel(Window* window, window_feel feel);

			/** @brief Changes the flags of a window.
			    @param window The window to update.
			    @param flags New flags bitmask. */
			void				SetWindowFlags(Window* window, uint32 flags);

			/** @brief Changes the title of a window.
			    @param window The window to update.
			    @param title New null-terminated title string. */
			void				SetWindowTitle(Window* window,
									const char* title);

			/** @brief Returns the window that currently has keyboard focus.
			    @return Pointer to the focus Window, or NULL. */
			Window*				FocusWindow() const { return fFocus; }

			/** @brief Returns the topmost (front) window.
			    @return Pointer to the front Window, or NULL. */
			Window*				FrontWindow() const { return fFront; }

			/** @brief Returns the bottommost (back) window.
			    @return Pointer to the back Window, or NULL. */
			Window*				BackWindow() const { return fBack; }

			/** @brief Returns the window at the given screen coordinate.
			    @param where Screen coordinate to test.
			    @return Pointer to the topmost Window at that point, or NULL. */
			Window*				WindowAt(BPoint where);

			/** @brief Returns the window currently receiving mouse events.
			    @return Pointer to the mouse-event Window, or NULL. */
			Window*				MouseEventWindow() const
									{ return fMouseEventWindow; }

			/** @brief Sets the window that receives mouse events.
			    @param window The new mouse-event window, or NULL. */
			void				SetMouseEventWindow(Window* window);

			/** @brief Records which view is currently under the mouse pointer.
			    @param window The window containing the view.
			    @param viewToken Token of the view under the mouse. */
			void				SetViewUnderMouse(const Window* window,
									int32 viewToken);

			/** @brief Returns the token of the view under the mouse in a given window.
			    @param window The window to query.
			    @return View token, or B_NULL_TOKEN. */
			int32				ViewUnderMouse(const Window* window);

			/** @brief Returns the event target that receives keyboard events.
			    @return Pointer to the keyboard EventTarget, or NULL. */
			EventTarget*		KeyboardEventTarget();

			/** @brief Sets the window that receives keyboard focus.
			    @param window The window to focus, or NULL to clear focus. */
			void				SetFocusWindow(Window* window = NULL);

			/** @brief Prevents focus from moving away from a specific window.
			    @param window The window to lock focus on. */
			void				SetFocusLocked(const Window* window);

			/** @brief Finds a window by its client-side token and team ID.
			    @param token Client token of the window.
			    @param teamID Team that owns the window.
			    @return Pointer to the matching Window, or NULL. */
			Window*				FindWindowByClientToken(int32 token,
									team_id teamID);

			/** @brief Finds an event target by its BMessenger.
			    @param messenger The BMessenger to look up.
			    @return Pointer to the matching EventTarget, or NULL. */
			EventTarget*		FindTarget(BMessenger& messenger);

			/** @brief Marks a region as dirty and schedules a redraw.
			    @param dirtyRegion Region that needs repainting.
			    @param exposeRegion Region newly exposed (e.g. by a window move). */
			void				MarkDirty(BRegion& dirtyRegion, BRegion& exposeRegion);

			/** @brief Marks a region as both dirty and exposed.
			    @param region Region to dirty. */
			void				MarkDirty(BRegion& region)
									{ return MarkDirty(region, region); }

			/** @brief Redraws all pending dirty regions. */
			void				Redraw();

			/** @brief Redraws the desktop background. */
			void				RedrawBackground();

			/** @brief Reloads the window decorator add-on, replacing the old one.
			    @param oldDecor The decorator being replaced.
			    @return true if the reload succeeded. */
			bool				ReloadDecor(DecorAddOn* oldDecor);

			/** @brief Returns the region currently used as the desktop background.
			    @return Reference to the background BRegion. */
			BRegion&			BackgroundRegion()
									{ return fBackgroundRegion; }

			/** @brief Minimises all windows belonging to an application.
			    @param team Team ID of the application. */
			void				MinimizeApplication(team_id team);

			/** @brief Brings all windows of an application to the front.
			    @param team Team ID of the application. */
			void				BringApplicationToFront(team_id team);

			/** @brief Performs an action on a window identified by its server token.
			    @param windowToken Server token of the window.
			    @param action Action code to perform. */
			void				WindowAction(int32 windowToken, int32 action);

			/** @brief Writes the ordered list of window tokens for a team to a link.
			    @param team Team ID to filter, or -1 for all.
			    @param sender LinkSender to write into. */
			void				WriteWindowList(team_id team,
									BPrivate::LinkSender& sender);

			/** @brief Writes detailed information about a window to a link.
			    @param serverToken Server token of the window.
			    @param sender LinkSender to write into. */
			void				WriteWindowInfo(int32 serverToken,
									BPrivate::LinkSender& sender);

			/** @brief Writes the application Z-order for a workspace to a link.
			    @param workspace Workspace index.
			    @param sender LinkSender to write into. */
			void				WriteApplicationOrder(int32 workspace,
									BPrivate::LinkSender& sender);

			/** @brief Writes the window Z-order for a workspace to a link.
			    @param workspace Workspace index.
			    @param sender LinkSender to write into. */
			void				WriteWindowOrder(int32 workspace,
									BPrivate::LinkSender& sender);

			//! The window lock must be held when accessing a window list!
			/** @brief Returns the window list for the current workspace.
			    @return Reference to the current WindowList. */
			WindowList&			CurrentWindows();

			/** @brief Returns the list of all windows across all workspaces.
			    @return Reference to the all-windows WindowList. */
			WindowList&			AllWindows();

			/** @brief Finds the window associated with a given client looper port.
			    @param port The client looper port_id.
			    @return Pointer to the matching Window, or NULL. */
			Window*				WindowForClientLooperPort(port_id port);

			/** @brief Returns the StackAndTile manager for this desktop.
			    @return Pointer to the StackAndTile instance. */
			StackAndTile*		GetStackAndTile() { return &fStackAndTile; }
private:
			WindowList&			_Windows(int32 index);

			void				_FlushPendingColors();

			void				_LaunchInputServer();
			void				_GetLooperName(char* name, size_t size);
			void				_PrepareQuit();
			void				_DispatchMessage(int32 code,
									BPrivate::LinkReceiver &link);

			void				_UpdateFloating(int32 previousWorkspace = -1,
									int32 nextWorkspace = -1,
									Window* mouseEventWindow = NULL);
			void				_UpdateBack();
			void				_UpdateFront(bool updateFloating = true);
			void				_UpdateFronts(bool updateFloating = true);
			bool				_WindowHasModal(Window* window) const;
			bool				_WindowCanHaveFocus(Window* window) const;

			void				_WindowChanged(Window* window);
			void				_WindowRemoved(Window* window);

			void				_ShowWindow(Window* window,
									bool affectsOtherWindows = true);
			void				_HideWindow(Window* window);

			void				_UpdateSubsetWorkspaces(Window* window,
									int32 previousIndex = -1,
									int32 newIndex = -1);
			void				_ChangeWindowWorkspaces(Window* window,
									uint32 oldWorkspaces, uint32 newWorkspaces);
			void				_BringWindowsToFront(WindowList& windows,
									int32 list, bool wereVisible);
			Window*				_LastFocusSubsetWindow(Window* window);
			bool				_CheckSendFakeMouseMoved(
									const Window* lastWindowUnderMouse);
			void				_SendFakeMouseMoved(Window* window = NULL);

			Screen*				_DetermineScreenFor(BRect frame);
			void				_RebuildClippingForAllWindows(
									BRegion& stillAvailableOnScreen);
			void				_TriggerWindowRedrawing(
									BRegion& dirtyRegion, BRegion& exposeRegion);
			void				_SetBackground(BRegion& background);

			status_t			_ActivateApp(team_id team);

			void				_SuspendDirectFrameBufferAccess();
			void				_ResumeDirectFrameBufferAccess();

			void				_ScreenChanged(Screen* screen);
			void				_SetCurrentWorkspaceConfiguration();
			void				_SetWorkspace(int32 index,
									bool moveFocusWindow = false);

private:
	friend class DesktopSettings;
	friend class LockedDesktopSettings;

			uid_t				fUserID;
			char*				fTargetScreen;
			::VirtualScreen		fVirtualScreen;
			ObjectDeleter<DesktopSettingsPrivate>
								fSettings;
			port_id				fMessagePort;
			::EventDispatcher	fEventDispatcher;
			area_id				fSharedReadOnlyArea;
			server_read_only_memory* fServerReadOnlyMemory;

			BLocker				fApplicationsLock;
			BObjectList<ServerApp> fApplications;

			sem_id				fShutdownSemaphore;
			int32				fShutdownCount;

			::Workspace::Private fWorkspaces[kMaxWorkspaces];
			MultiLocker			fScreenLock;
			BLocker				fDirectScreenLock;
			team_id				fDirectScreenTeam;
			int32				fCurrentWorkspace;
			int32				fPreviousWorkspace;

			WindowList			fAllWindows;
			WindowList			fSubsetWindows;
			WindowList			fFocusList;
			Window*				fLastWorkspaceFocus[kMaxWorkspaces];

			BObjectList<WorkspacesView> fWorkspacesViews;
			BLocker				fWorkspacesLock;

			CursorManager		fCursorManager;
			ServerCursorReference fCursor;
			ServerCursorReference fManagementCursor;

			MultiLocker			fWindowLock;

			BRegion				fBackgroundRegion;
			BRegion				fScreenRegion;

			Window*				fMouseEventWindow;
			const Window*		fWindowUnderMouse;
			const Window*		fLockedFocusWindow;
			int32				fViewUnderMouse;
			BPoint				fLastMousePosition;
			int32				fLastMouseButtons;

			Window*				fFocus;
			Window*				fFront;
			Window*				fBack;

			StackAndTile		fStackAndTile;

			BMessage			fPendingColors;
};

#endif	// DESKTOP_H
