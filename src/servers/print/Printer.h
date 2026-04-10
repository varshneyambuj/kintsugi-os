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
 *   Copyright 2001-2010, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ithamar R. Adema
 *       Michael Pfeiffer
 */

/** @file Printer.h
 *  @brief Printer definition management, spool job processing, and driver interaction. */

#ifndef PRINTER_H
#define PRINTER_H

class Printer;

#include "BeUtils.h"
#include "ResourceManager.h"
#include "Jobs.h"

	// BeOS API
#include <Directory.h>
#include <Handler.h>
#include <image.h>
#include <Locker.h>
#include <PrintJob.h>
#include <String.h>

	// OpenTracker shared sources
#include "ObjectList.h"


/** @brief Watches a printer spool directory and notifies the server of new jobs. */
class SpoolFolder : public Folder {
protected:
	/** @brief Notify the print server when a spool job is added or changed. */
	void Notify(Job* job, int kind);

public:
	/** @brief Construct a spool folder watcher for the given directory. */
	SpoolFolder(BLocker* locker, BLooper* looper, const BDirectory& spoolDir);
};


/** @brief Represents a single printer definition and manages its print jobs. */
class Printer : public BHandler, public Object
{
	typedef BHandler Inherited;
public:
	/** @brief Construct a printer from its definition directory and shared resource. */
								Printer(const BDirectory* node, Resource* res);
	/** @brief Destructor notifying the server of this printer's deletion. */
	virtual						~Printer();

	/** @brief Handle incoming messages including scripting commands. */
	virtual	void				MessageReceived(BMessage* message);
	/** @brief Return the scripting suites supported by this printer. */
	virtual	status_t			GetSupportedSuites(BMessage* msg);
	/** @brief Resolve a scripting specifier to the appropriate handler. */
	virtual	BHandler*			ResolveSpecifier(BMessage* msg, int32 index,
									BMessage* spec, int32 form, const char* prop);

			// Static helper functions
	/** @brief Find a printer by name in the global list. */
	static	Printer*			Find(const BString& name);
	/** @brief Find a printer by node reference in the global list. */
	static	Printer*			Find(node_ref* node);
	/** @brief Return the printer at the given index. */
	static	Printer*			At(int32 idx);
	/** @brief Remove a printer from the global list. */
	static	void				Remove(Printer* printer);
	/** @brief Return the total number of registered printers. */
	static	int32				CountPrinters();

	/** @brief Remove this printer's spool directory from disk. */
			status_t			Remove();
	/** @brief Locate the filesystem path to the named printer driver. */
	static	status_t			FindPathToDriver(const char* driver,
									BPath* path);
	/** @brief Invoke the driver's add_printer configuration function. */
	static	status_t			ConfigurePrinter(const char* driverName,
									const char* printerName);
	/** @brief Open the driver's job setup configuration dialog. */
			status_t			ConfigureJob(BMessage& ioSettings);
	/** @brief Open the driver's page setup configuration dialog. */
			status_t			ConfigurePage(BMessage& ioSettings);
	/** @brief Retrieve default settings from the printer driver add-on. */
			status_t			GetDefaultSettings(BMessage& configuration);

			// Try to start processing of next spooled job
	/** @brief Process the next waiting spooled job, if any. */
			void				HandleSpooledJob();

			// Abort print_thread without processing spooled job
	/** @brief Signal the print thread to abort without processing. */
			void				AbortPrintThread();

			// Scripting support, see Printer.Scripting.cpp
	/** @brief Handle a scripting command for this printer. */
			void				HandleScriptingCommand(BMessage* msg);

	/** @brief Retrieve this printer's name from its spool directory attributes. */
			void				GetName(BString& name);
	/** @brief Return the shared resource associated with this printer. */
			Resource*			GetResource() { return fResource; }

private:
			status_t			GetDriverName(BString* name);
			void				AddCurrentPrinter(BMessage& message);

			BDirectory*			SpoolDir() { return fPrinter.GetSpoolDir(); }

			void				ResetJobStatus();
			bool				HasCurrentPrinter(BString& name);
			bool				MoveJob(const BString& name);

			// Get next spooled job if any
			bool				FindSpooledJob();
			status_t			PrintSpooledJob(const char* spoolFile);
			void				PrintThread(Job* job);

	static	status_t			print_thread(void* data);
			void				StartPrintThread();

private:
			SpoolFolder			fPrinter; /**< The printer spooling directory */
			Resource*			fResource; /**< Shared resource for this printer's transport */
			bool				fSinglePrintThread; /**< Whether only one job may print at a time */
			Job*				fJob; /**< The next job to process */
			int32				fProcessing; /**< Current number of active processing threads */
			bool				fAbort; /**< Flag to stop processing */
	static	BObjectList<Printer> sPrinters; /**< Global list of registered printers */
};

#endif
