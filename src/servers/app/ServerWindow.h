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
 * Copyright 2001-2009, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Adrian Oanca <adioanca@gmail.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerWindow.h
 *  @brief Server-side representative of a client BWindow, bridging protocol messages to Window. */

#ifndef SERVER_WINDOW_H
#define SERVER_WINDOW_H


#include <AutoDeleter.h>
#include <GraphicsDefs.h>
#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <Rect.h>
#include <Region.h>
#include <String.h>
#include <Window.h>

#include <PortLink.h>
#include <TokenSpace.h>

#include "EventDispatcher.h"
#include "MessageLooper.h"


class BString;
class BMessenger;
class BPoint;
class BMessage;

class Desktop;
class ServerApp;
class Decorator;
class Window;
class Workspace;
class View;
class ServerPicture;
class DirectWindowInfo;
struct window_info;

#define AS_UPDATE_DECORATOR 'asud'
#define AS_UPDATE_COLORS 'asuc'
#define AS_UPDATE_FONTS 'asuf'


/** @brief Handles the server-side communication thread for a single client BWindow. */
class ServerWindow : public MessageLooper {
public:
	/** @brief Constructs a ServerWindow for the given client window.
	 *  @param title       Window title string.
	 *  @param app         ServerApp that owns this window.
	 *  @param clientPort  Reply port for the client BWindow.
	 *  @param looperPort  Port of the client's BLooper.
	 *  @param clientToken Token identifying the client handler. */
								ServerWindow(const char *title, ServerApp *app,
									port_id clientPort, port_id looperPort,
									int32 clientToken);
	virtual						~ServerWindow();

	/** @brief Finishes initialisation, creating the Window object.
	 *  @param frame     Initial window frame.
	 *  @param look      Window look (decoration style).
	 *  @param feel      Window feel (modality / type).
	 *  @param flags     Window flags bitfield.
	 *  @param workspace Initial workspace bitmask.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			Init(BRect frame, window_look look,
									window_feel feel, uint32 flags,
									uint32 workspace);

	/** @brief Returns the port on which this ServerWindow receives messages.
	 *  @return Message port ID. */
	virtual port_id				MessagePort() const { return fMessagePort; }

	/** @brief Returns the event target for this window's input events.
	 *  @return Reference to the EventTarget. */
			::EventTarget&		EventTarget() { return fEventTarget; }

	/** @brief Returns the owning ServerApp.
	 *  @return Pointer to the ServerApp. */
	inline	ServerApp*			App() const { return fServerApp; }

	/** @brief Returns the Desktop this window lives on.
	 *  @return Pointer to the Desktop. */
			::Desktop*			Desktop() const { return fDesktop; }

	/** @brief Returns the Window object managed by this ServerWindow.
	 *  @return Pointer to the Window. */
			::Window*			Window() const;

	// methods for sending various messages to client.
	/** @brief Sends a quit-requested notification to the client BWindow. */
			void				NotifyQuitRequested();

	/** @brief Sends a minimise/restore notification to the client BWindow.
	 *  @param minimize true to minimise, false to restore. */
			void				NotifyMinimize(bool minimize);

	/** @brief Sends a zoom notification to the client BWindow. */
			void				NotifyZoom();

	// util methods.
	/** @brief Returns the messenger targeting the window's focus handler.
	 *  @return Const reference to the focus BMessenger. */
			const BMessenger&	FocusMessenger() const
									{ return fFocusMessenger; }

	/** @brief Returns the messenger targeting the window's handler.
	 *  @return Const reference to the handler BMessenger. */
			const BMessenger&	HandlerMessenger() const
									{ return fHandlerMessenger; }

	/** @brief Handles a screen-changed notification, forwarding it to the client.
	 *  @param message Message containing new screen parameters. */
			void				ScreenChanged(const BMessage* message);

	/** @brief Sends a BMessage to the client-side BWindow.
	 *  @param message Message to deliver.
	 *  @param target  Handler token on the client side (default B_NULL_TOKEN).
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SendMessageToClient(const BMessage* message,
									int32 target = B_NULL_TOKEN) const;

	/** @brief Creates and returns the Window object for this ServerWindow.
	 *  @param frame     Initial frame.
	 *  @param name      Window title.
	 *  @param look      Window look.
	 *  @param feel      Window feel.
	 *  @param flags     Window flags.
	 *  @param workspace Workspace bitmask.
	 *  @return Pointer to the newly created Window. */
	virtual	::Window*			MakeWindow(BRect frame, const char* name,
									window_look look, window_feel feel,
									uint32 flags, uint32 workspace);

	/** @brief Changes the window title and notifies the client.
	 *  @param newTitle New title string. */
			void				SetTitle(const char* newTitle);

	/** @brief Returns the window's current title string.
	 *  @return Null-terminated title. */
	inline	const char*			Title() const { return fTitle; }

	// related thread/team_id(s).
	/** @brief Returns the team ID of the client process.
	 *  @return Client team ID. */
	inline	team_id				ClientTeam() const { return fClientTeam; }

	/** @brief Returns the client BLooper's port ID.
	 *  @return Client looper port. */
	inline	port_id				ClientLooperPort () const
									{ return fClientLooperPort; }

	/** @brief Returns the client-side handler token.
	 *  @return Client token. */
	inline	int32				ClientToken() const { return fClientToken; }

	/** @brief Returns the server-side token for this window.
	 *  @return Server token. */
	inline	int32				ServerToken() const { return fServerToken; }

	/** @brief Schedules a redraw of the window's dirty regions. */
			void				RequestRedraw();

	/** @brief Fills in a window_info structure with current window properties.
	 *  @param info Reference that receives window information. */
			void				GetInfo(window_info& info);

	/** @brief Handles a direct-access buffer state change notification.
	 *  @param bufferState New buffer state flags.
	 *  @param driverState Driver state flags (default 0). */
			void				HandleDirectConnection(int32 bufferState,
									int32 driverState = 0);

	/** @brief Returns whether the window has direct frame buffer access enabled.
	 *  @return true if direct window info is set. */
			bool				HasDirectFrameBufferAccess() const
									{ return fDirectWindowInfo.IsSet(); }

	/** @brief Returns whether the window is currently directly accessing the frame buffer.
	 *  @return true if direct access is active. */
			bool				IsDirectlyAccessing() const
									{ return fIsDirectlyAccessing; }

	/** @brief Re-synchronises the current view's drawing state from the server. */
			void				ResyncDrawState();

						// TODO: Change this
	/** @brief Updates the current drawing region to reflect clipping changes. */
	inline	void				UpdateCurrentDrawingRegion()
									{ _UpdateCurrentDrawingRegion(); };

private:
			View*				_CreateView(BPrivate::LinkReceiver &link,
									View **_parent);

			void				_Show();
			void				_Hide();

			// message handling methods.
			void				_DispatchMessage(int32 code,
									BPrivate::LinkReceiver &link);
			void				_DispatchViewMessage(int32 code,
									BPrivate::LinkReceiver &link);
			void				_DispatchViewDrawingMessage(int32 code,
									BPrivate::LinkReceiver &link);
			bool				_DispatchPictureMessage(int32 code,
									BPrivate::LinkReceiver &link);
			void				_MessageLooper();
	virtual void				_PrepareQuit();
	virtual void				_GetLooperName(char* name, size_t size);

			void				_ResizeToFullScreen();
			status_t			_EnableDirectWindowMode();
			void				_DirectWindowSetFullScreen(bool set);

			void				_SetCurrentView(View* view);
			void				_UpdateDrawState(View* view);
			void				_UpdateCurrentDrawingRegion();

			bool				_MessageNeedsAllWindowsLocked(
									uint32 code) const;

private:
			char*				fTitle;

			::Desktop*			fDesktop;
			ServerApp*			fServerApp;
			ObjectDeleter< ::Window>
								fWindow;
			bool				fWindowAddedToDesktop;

			team_id				fClientTeam;

			port_id				fMessagePort;
			port_id				fClientReplyPort;
			port_id				fClientLooperPort;
			BMessenger			fFocusMessenger;
			BMessenger			fHandlerMessenger;
			::EventTarget		fEventTarget;

			int32				fRedrawRequested;

			int32				fServerToken;
			int32				fClientToken;

			View*				fCurrentView;
			BRegion				fCurrentDrawingRegion;
			bool				fCurrentDrawingRegionValid;

			ObjectDeleter<DirectWindowInfo>
								fDirectWindowInfo;
			bool				fIsDirectlyAccessing;
};

#endif	// SERVER_WINDOW_H
