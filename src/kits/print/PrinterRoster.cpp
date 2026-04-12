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
 *   Copyright 2008 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors: Julun, <host.haiku@gmx.de>
 */


/**
 * @file PrinterRoster.cpp
 * @brief BPrinterRoster — directory enumerator for installed printers.
 *
 * BPrinterRoster iterates the user printers directory, providing sequential
 * and search-by-name access to BPrinter objects. It also supports node-monitor
 * watching of the printers directory so callers can react to printers being
 * installed or removed.
 *
 * @see BPrinter, BJobSetupPanel
 */


#include <PrinterRoster.h>

#include <FindDirectory.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Printer.h>


#include <new>


namespace BPrivate {
	namespace Print {


/**
 * @brief Constructs a BPrinterRoster and opens the user printers directory.
 *
 * Resolves B_USER_PRINTERS_DIRECTORY, stores a node_ref to it, and opens a
 * BDirectory iterator on it. Both are used for subsequent enumeration calls.
 */
BPrinterRoster::BPrinterRoster()
	: fListener(NULL)
{
	BPath path;
	find_directory(B_USER_PRINTERS_DIRECTORY, &path, true);
	BNode(path.Path()).GetNodeRef(&fUserPrintersNodRef);

	fUserPrintersDirectory.SetTo(&fUserPrintersNodRef);
}


/**
 * @brief Destroys the BPrinterRoster and stops any active directory watcher.
 */
BPrinterRoster::~BPrinterRoster()
{
	StopWatching();
}


/**
 * @brief Returns the total number of valid printers installed for the current user.
 *
 * Rewinds the directory, counts all entries that pass BPrinter::IsValid(), then
 * rewinds again to leave the iterator in its initial state.
 *
 * @return The number of valid installed printers.
 */
int32
BPrinterRoster::CountPrinters()
{
	Rewind();

	int32 i = 0;
	BPrinter printer;
	while (GetNextPrinter(&printer) == B_OK)
		i++;

	Rewind();
	return i;
}


/**
 * @brief Advances the iterator and fills \a printer with the next valid printer.
 *
 * Skips directory entries that do not represent valid printers (wrong MIME type
 * or missing attributes) until a valid one is found or the directory is exhausted.
 * Calls BPrinter::Unset() on \a printer if no further valid printers exist.
 *
 * @param printer  Receives the next valid BPrinter on success.
 * @return B_OK if a valid printer was found, or an error code when exhausted.
 */
status_t
BPrinterRoster::GetNextPrinter(BPrinter* printer)
{
	if (!printer)
		return B_BAD_VALUE;

	status_t status = fUserPrintersDirectory.InitCheck();
	if (!status == B_OK)
		return status;

	BEntry entry;
	bool next = true;
	while (status == B_OK && next) {
		status = fUserPrintersDirectory.GetNextEntry(&entry);
		if (status == B_OK) {
			printer->SetTo(entry);
			next = !printer->IsValid();
		} else {
			printer->Unset();
		}
	}
	return status;
}


/**
 * @brief Finds and returns the system default printer.
 *
 * Iterates all entries in the user printers directory and returns the first
 * one that is both valid and marked as default. Calls BPrinter::Unset() on
 * \a printer and returns B_ERROR if no default printer is found.
 *
 * @param printer  Receives the default BPrinter on success.
 * @return B_OK if a default printer was found, B_ERROR otherwise.
 */
status_t
BPrinterRoster::GetDefaultPrinter(BPrinter* printer)
{
	if (!printer)
		return B_BAD_VALUE;

	BDirectory dir(&fUserPrintersNodRef);
	status_t status = dir.InitCheck();
	if (!status == B_OK)
		return status;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsDirectory())
			continue;

		printer->SetTo(entry);
		if (printer->IsValid() && printer->IsDefault())
			return B_OK;
	}
	printer->Unset();
	return B_ERROR;
}


/**
 * @brief Finds the printer with a given display name.
 *
 * Iterates all directory entries and returns the first valid printer whose
 * Name() matches \a name exactly.
 *
 * @param name     The display name to search for.
 * @param printer  Receives the matching BPrinter on success.
 * @return B_OK if the printer was found, B_BAD_VALUE if arguments are invalid,
 *         or B_ERROR if no match was found.
 */
status_t
BPrinterRoster::FindPrinter(const BString& name, BPrinter* printer)
{
	if (name.Length() <= 0 || !printer)
		return B_BAD_VALUE;

	BDirectory dir(&fUserPrintersNodRef);
	status_t status = dir.InitCheck();
	if (!status == B_OK)
		return status;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsDirectory())
			continue;

		printer->SetTo(entry);
		if (printer->IsValid() && printer->Name() == name)
			return B_OK;
	}
	printer->Unset();
	return B_ERROR;

}


/**
 * @brief Rewinds the internal directory iterator to the beginning.
 *
 * @return B_OK on success, or an error code from BDirectory::Rewind().
 */
status_t
BPrinterRoster::Rewind()
{
	return fUserPrintersDirectory.Rewind();
}


/**
 * @brief Starts watching the user printers directory for changes.
 *
 * Stores a copy of \a listener and registers B_WATCH_DIRECTORY monitoring
 * on the user printers node_ref.
 *
 * @param listener  A valid BMessenger to receive node-monitor notifications.
 * @return B_OK on success, B_BAD_VALUE if the messenger is invalid, or
 *         B_NO_MEMORY if the messenger copy cannot be allocated.
 */
status_t
BPrinterRoster::StartWatching(const BMessenger& listener)
{
	StopWatching();

	if (!listener.IsValid())
		return B_BAD_VALUE;

	fListener = new(std::nothrow) BMessenger(listener);
	if (!fListener)
		return B_NO_MEMORY;

	return watch_node(&fUserPrintersNodRef, B_WATCH_DIRECTORY, *fListener);
}


/**
 * @brief Stops watching the user printers directory for changes.
 *
 * Cancels the node-monitor registration and frees the listener messenger.
 */
void
BPrinterRoster::StopWatching()
{
	if (fListener) {
		stop_watching(*fListener);
		delete fListener;
		fListener = NULL;
	}
}


	}	// namespace Print
}	// namespace BPrivate
