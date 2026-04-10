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
 *   Copyright 2001-2015, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ithamar R. Adema
 *       Michael Pfeiffer
 */

/** @file PrintServerApp.h
 *  @brief Main print server application managing printers, transports, and spooled jobs. */

#ifndef _PRINT_SERVER_APP_H
#define _PRINT_SERVER_APP_H


#include <Bitmap.h>
#include <Catalog.h>
#include <OS.h>
#include <Server.h>
#include <String.h>

#include "FolderWatcher.h"
#include "ResourceManager.h"
#include "Settings.h"


class Printer;
class Transport;


// The global BLocker for synchronisation.
extern BLocker *gLock;


/** @brief The print_server application managing printer lifecycle and print jobs. */
class PrintServerApp : public BServer, public FolderListener {
private:
		typedef BServer Inherited;

public:
	/** @brief Construct the print server, initializing printers and transports. */
								PrintServerApp(status_t* error);
	/** @brief Destructor saving settings and cleaning up. */
								~PrintServerApp();

	/** @brief Increment the reference count, blocking quit until released. */
			void				Acquire();
	/** @brief Decrement the reference count, allowing quit when zero. */
			void				Release();

	/** @brief Handle quit request, releasing all printers first. */
	virtual	bool				QuitRequested();
	/** @brief Dispatch incoming messages to appropriate handlers. */
	virtual	void				MessageReceived(BMessage* msg);
	/** @brief Notify the server that a printer is being deleted. */
			void				NotifyPrinterDeletion(Printer* printer);

	// Scripting support, see PrintServerApp.Scripting.cpp
	/** @brief Return the scripting suites supported by this application. */
	virtual	status_t			GetSupportedSuites(BMessage* msg);
	/** @brief Handle a scripting command message. */
			void				HandleScriptingCommand(BMessage* msg);
	/** @brief Resolve a printer from a scripting specifier message. */
			Printer*			GetPrinterFromSpecifier(BMessage* msg);
	/** @brief Resolve a transport from a scripting specifier message. */
			Transport*			GetTransportFromSpecifier(BMessage* msg);
	/** @brief Resolve a scripting specifier to the appropriate handler. */
	virtual	BHandler*			ResolveSpecifier(BMessage* msg, int32 index,
									BMessage* specifier, int32 form,
									const char* property);

private:
			bool				OpenSettings(BFile& file, const char* name,
									bool forReading);
			void				LoadSettings();
			void				SaveSettings();

			status_t			SetupPrinterList();

			void				HandleSpooledJobs();

			status_t			SelectPrinter(const char* printerName);
			status_t			CreatePrinter(const char* printerName,
									const char* driverName,
									const char* connection,
									const char* transportName,
									const char* transportPath);

			void				RegisterPrinter(BDirectory* node);
			void				UnregisterPrinter(Printer* printer);

	// FolderListener
			void				EntryCreated(node_ref* node, entry_ref* entry);
			void				EntryRemoved(node_ref* node);
			void				AttributeChanged(node_ref* node);

			status_t			StoreDefaultPrinter();
			status_t			RetrieveDefaultPrinter();

			status_t			FindPrinterNode(const char* name, BNode& node);

		// "Classic" BeOS R5 support, see PrintServerApp.R5.cpp
	static	status_t			async_thread(void* data);
			void				AsyncHandleMessage(BMessage* msg);
			void				Handle_BeOSR5_Message(BMessage* msg);

private:
			ResourceManager		fResourceManager; /**< Manages shared transport resources */
			Printer*			fDefaultPrinter; /**< Currently active default printer */
			size_t				fIconSize; /**< Size of the selected printer icon */
			uint8*				fSelectedIcon; /**< Icon data for the default printer */
			int32				fReferences; /**< Active reference count */
			sem_id				fHasReferences; /**< Semaphore blocking quit while refs > 0 */
			Settings*			fSettings; /**< Persistent print server settings */
			bool				fUseConfigWindow; /**< Whether to show the config dialog */
			FolderWatcher*		fFolder; /**< Node monitor for the printers directory */
};


#endif	// _PRINT_SERVER_APP_H
