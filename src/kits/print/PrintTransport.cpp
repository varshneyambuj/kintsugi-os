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
 *   Copyright (c) 2004 Haiku.
 *   Authors: Philippe Houdoin, Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file PrintTransport.cpp
 * @brief PrintTransport — loader and wrapper for print transport add-ons.
 *
 * PrintTransport locates, loads, and initialises a named transport add-on
 * from the standard add-on directory hierarchy. It resolves the init_transport
 * and exit_transport entry points, calls init_transport to obtain a BDataIO
 * object, and calls exit_transport on destruction. The resulting BDataIO can be
 * used directly to stream raw print data to the transport back-end.
 *
 * @see PrintTransportAddOn, PrinterDriverAddOn
 */


#include "PrintTransport.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

// implementation of class PrintTransport

/**
 * @brief Constructs an uninitialised PrintTransport.
 *
 * All members are set to safe defaults; call Open() to load a transport add-on.
 */
PrintTransport::PrintTransport()
	: fDataIO(NULL)
	, fAddOnID(-1)
	, fExitProc(NULL)
{
}

/**
 * @brief Destroys the PrintTransport, calling exit_transport and unloading the add-on.
 *
 * If exit_transport was resolved, it is called before the add-on image is
 * unloaded to allow the transport to flush and close its channel cleanly.
 */
PrintTransport::~PrintTransport()
{
	if (fExitProc) {
		(*fExitProc)();
		fExitProc = NULL;
	}

	if (fAddOnID >= 0) {
		unload_add_on(fAddOnID);
		fAddOnID = -1;
	}
}

/**
 * @brief Opens a transport add-on for the printer described by \a printerFolder.
 *
 * Reads the "transport" node attribute from \a printerFolder to determine the
 * add-on name, then searches the standard add-on directories under
 * "Print/transport". Once found, the add-on is loaded and init_transport is
 * called with the printer folder's path. The resulting BDataIO is stored in
 * fDataIO for retrieval via GetDataIO().
 *
 * @param printerFolder  BNode pointing to the printer's spool directory.
 * @return B_OK on success, or B_ERROR if the attribute is missing, the add-on
 *         cannot be loaded, or the init/exit entry points are absent.
 */
status_t PrintTransport::Open(BNode* printerFolder)
{
	// already opened?
	if (fDataIO != NULL) {
		return B_ERROR;
	}

	// retrieve transport add-on name from printer folder attribute
	BString transportName;
	if (printerFolder->ReadAttrString("transport", &transportName) != B_OK) {
		return B_ERROR;
	}

	const directory_which paths[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
	};
	BPath path;
	for (uint32 i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
		if (find_directory(paths[i], &path) != B_OK)
			continue;
	path.Append("Print/transport");
	path.Append(transportName.String());
	fAddOnID = load_add_on(path.Path());
		if (fAddOnID >= 0)
			break;
	}

	if (fAddOnID < 0) {
		// failed to load transport add-on
		return B_ERROR;
	}

	// get init & exit proc
	BDataIO* (*initProc)(BMessage*);
	get_image_symbol(fAddOnID, "init_transport", B_SYMBOL_TYPE_TEXT, (void **) &initProc);
	get_image_symbol(fAddOnID, "exit_transport", B_SYMBOL_TYPE_TEXT, (void **) &fExitProc);

	if (initProc == NULL || fExitProc == NULL) {
		// transport add-on has not the proper interface
		return B_ERROR;
	}

	// now, initialize the transport add-on
	node_ref   ref;
	BDirectory dir;

	printerFolder->GetNodeRef(&ref);
	dir.SetTo(&ref);

	if (path.SetTo(&dir, NULL) != B_OK) {
		return B_ERROR;
	}

	// request BDataIO object from transport add-on
	BMessage input('TRIN');
	input.AddString("printer_file", path.Path());
	fDataIO = (*initProc)(&input);
	return B_OK;
}

/**
 * @brief Returns the BDataIO object obtained from the transport add-on.
 *
 * @return Pointer to the active BDataIO, or NULL if Open() has not been called
 *         or the transport could not be initialised.
 */
BDataIO*
PrintTransport::GetDataIO()
{
	return fDataIO;
}

/**
 * @brief Returns true if the "Print To File" transport was cancelled by the user.
 *
 * The BeOS "Print To File" transport returns a non-NULL BDataIO even when the
 * user cancels the file-picker. This method detects that edge case by checking
 * whether fDataIO is a BFile whose InitCheck() fails.
 *
 * @return true if no transport is active or if the transport is a BFile that
 *         failed to initialise (indicating user cancellation).
 */
bool PrintTransport::IsPrintToFileCanceled() const
{
	// The BeOS "Print To File" transport add-on returns a non-NULL BDataIO *
	// even after user filepanel cancellation!
	BFile* file = dynamic_cast<BFile*>(fDataIO);
	return fDataIO == NULL || (file != NULL && file->InitCheck() != B_OK);
}
