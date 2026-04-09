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
 * Copyright 2001-2013, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Adrian Oanca <adioanca@cotty.iren.ro>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerApp.h
 *  @brief Server-side representative of a connected client BApplication. */

#ifndef SERVER_APP_H
#define SERVER_APP_H


#include "AppFontManager.h"
#include "ClientMemoryAllocator.h"
#include "MessageLooper.h"
#include "ServerFont.h"

#include <ObjectList.h>
#include <TokenSpace.h>

#include <Messenger.h>
#include <String.h>


class AreaPool;
class BMessage;
class BList;
class Desktop;
class DrawingEngine;
class ServerPicture;
class ServerCursor;
class ServerBitmap;
class ServerWindow;

namespace BPrivate {
	class PortLink;
};


/** @brief Represents a client BApplication inside app_server, managing its windows,
 *         bitmaps, pictures, cursors, and fonts. */
class ServerApp : public MessageLooper {
public:
	/** @brief Constructs the server-side application object.
	 *  @param desktop         Desktop this application belongs to.
	 *  @param clientAppPort   Port used to send messages to the client app.
	 *  @param clientLooperPort Port of the client's BLooper.
	 *  @param clientTeamID    Team ID of the client process.
	 *  @param handlerID       Token of the initial handler.
	 *  @param signature       Application MIME signature. */
								ServerApp(Desktop* desktop,
									port_id clientAppPort,
									port_id clientLooperPort,
									team_id clientTeamID, int32 handlerID,
									const char* signature);
	virtual						~ServerApp();

	/** @brief Checks whether the object was constructed successfully.
	 *  @return B_OK if initialised, an error code otherwise. */
			status_t			InitCheck();

	/** @brief Requests the application's message looper to quit. */
	virtual	void				Quit();

	/** @brief Signals the application to quit and posts to a semaphore when done.
	 *  @param shutdownSemaphore Semaphore released after cleanup. */
			void				Quit(sem_id shutdownSemaphore);

	/** @brief Returns the port on which this ServerApp receives messages.
	 *  @return Message port ID. */
	virtual	port_id				MessagePort() const { return fMessagePort; }

	/*!
		\brief Determines whether the application is the active one
		\return true if active, false if not.
	*/
	/** @brief Returns whether this application is currently the active one.
	 *  @return true if active. */
			bool				IsActive() const { return fIsActive; }

	/** @brief Activates or deactivates this application.
	 *  @param value true to activate, false to deactivate. */
			void				Activate(bool value);

	/** @brief Sends a BMessage to the client application.
	 *  @param message Message to deliver. */
			void				SendMessageToClient(BMessage* message) const;

	/** @brief Sets the cursor currently used by this application.
	 *  @param cursor New cursor to use. */
			void				SetCurrentCursor(ServerCursor* cursor);

	/** @brief Returns the cursor currently used by this application.
	 *  @return Pointer to the active ServerCursor. */
			ServerCursor*		CurrentCursor() const;

	/** @brief Returns the client process's team ID.
	 *  @return Team ID. */
			team_id				ClientTeam() const { return fClientTeam; }

	/** @brief Returns the full MIME signature of the application.
	 *  @return Null-terminated signature string. */
			const char*			Signature() const
									{ return fSignature.String(); }

	/** @brief Returns the leaf part of the MIME signature (after "application/").
	 *  @return Pointer into the signature string, past the 12-char prefix. */
			const char*			SignatureLeaf() const
									{ return fSignature.String() + 12; }

	/** @brief Registers a new ServerWindow with this application.
	 *  @param window Window to add.
	 *  @return true on success. */
			bool				AddWindow(ServerWindow* window);

	/** @brief Unregisters a ServerWindow from this application.
	 *  @param window Window to remove. */
			void				RemoveWindow(ServerWindow* window);

	/** @brief Returns whether any window of this application is on the given workspace.
	 *  @param index Zero-based workspace index.
	 *  @return true if a window is present on that workspace. */
			bool				InWorkspace(int32 index) const;

	/** @brief Returns the workspace bitmask covering all application windows.
	 *  @return Bitfield of workspaces. */
			uint32				Workspaces() const;

	/** @brief Returns the workspace index that was active when the app launched.
	 *  @return Initial workspace index. */
			int32				InitialWorkspace() const
									{ return fInitialWorkspace; }

	/** @brief Looks up a ServerBitmap by token.
	 *  @param token Token identifying the bitmap.
	 *  @return Pointer to the ServerBitmap, or NULL if not found. */
			ServerBitmap*		GetBitmap(int32 token) const;

	/** @brief Creates a new ServerPicture, optionally cloned from an original.
	 *  @param original Picture to clone, or NULL for a fresh picture.
	 *  @return Pointer to the new ServerPicture, or NULL on failure. */
			ServerPicture*		CreatePicture(
									const ServerPicture* original = NULL);

	/** @brief Looks up a ServerPicture by token.
	 *  @param token Token identifying the picture.
	 *  @return Pointer to the ServerPicture, or NULL if not found. */
			ServerPicture*		GetPicture(int32 token) const;

	/** @brief Registers an existing ServerPicture with this application.
	 *  @param picture Picture to register.
	 *  @return true on success. */
			bool				AddPicture(ServerPicture* picture);

	/** @brief Unregisters a ServerPicture from this application.
	 *  @param picture Picture to remove. */
			void				RemovePicture(ServerPicture* picture);

	/** @brief Returns the Desktop this application is associated with.
	 *  @return Pointer to the Desktop. */
			Desktop*			GetDesktop() const { return fDesktop; }

	/** @brief Returns the plain (body-text) font for this application.
	 *  @return Reference to the plain ServerFont. */
			const ServerFont&	PlainFont() const { return fPlainFont; }

	/** @brief Returns the token space used for view objects.
	 *  @return Reference to the view token space. */
			BPrivate::BTokenSpace& ViewTokens() { return fViewTokens; }

	/** @brief Returns the per-application font manager.
	 *  @return Pointer to the AppFontManager. */
			AppFontManager*		FontManager() { return fAppFontManager; }

private:
	virtual	void				_GetLooperName(char* name, size_t size);
	virtual	void				_DispatchMessage(int32 code,
									BPrivate::LinkReceiver& link);
	virtual	void				_MessageLooper();
			status_t			_CreateWindow(int32 code,
									BPrivate::LinkReceiver& link,
									port_id& clientReplyPort);

			bool				_HasWindowUnderMouse();

			bool				_AddBitmap(ServerBitmap* bitmap);
			void				_DeleteBitmap(ServerBitmap* bitmap);
			ServerBitmap*		_FindBitmap(int32 token) const;

			ServerPicture*		_FindPicture(int32 token) const;

private:
	typedef std::map<int32, BReference<ServerBitmap> > BitmapMap;
	typedef std::map<int32, BReference<ServerPicture> > PictureMap;

			port_id				fMessagePort;
			port_id				fClientReplyPort;
									// our BApplication's event port

			BMessenger			fHandlerMessenger;
			port_id				fClientLooperPort;
			int32				fClientToken;
									// To send a BMessage to the client
									// (port + token)

			Desktop*			fDesktop;
			BString				fSignature;
			team_id				fClientTeam;

			ServerFont			fPlainFont;
			ServerFont			fBoldFont;
			ServerFont			fFixedFont;

	mutable	BLocker				fWindowListLock;
			BObjectList<ServerWindow> fWindowList;
			BPrivate::BTokenSpace fViewTokens;

			int32				fInitialWorkspace;
			uint32				fTemporaryDisplayModeChange;

			// NOTE: Bitmaps and Pictures are stored globally, but ServerApps
			// remember which ones they own so that they can destroy them when
			// they quit.
	mutable	BLocker				fMapLocker;
			BitmapMap			fBitmapMap;
			PictureMap			fPictureMap;

			BReference<ServerCursor>
								fAppCursor;
			BReference<ServerCursor>
								fViewCursor;
			int32				fCursorHideLevel;
									// 0 = cursor visible

			bool				fIsActive;

			BReference<ClientMemoryAllocator> fMemoryAllocator;

			AppFontManager*		fAppFontManager;
};


#endif	// SERVER_APP_H
