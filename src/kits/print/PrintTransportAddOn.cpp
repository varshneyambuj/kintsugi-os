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
 */


/**
 * @file PrintTransportAddOn.cpp
 * @brief Standard C entry points for a print transport add-on.
 *
 * Implements the two exported C symbols that the print subsystem expects from
 * every transport add-on: init_transport() and exit_transport(). A single
 * global BDataIO pointer enforces the single-instance constraint; subclasses
 * of PrintTransportAddOn provide instantiate_transport() to create the concrete
 * transport object.
 *
 * @see PrintTransport, PrintTransportAddOn.h
 */


#include "PrintTransportAddOn.h"

// We don't support multiple instances of the same transport add-on
/** @brief Global pointer to the single active transport BDataIO instance. */
static BDataIO* gTransport = NULL;

/**
 * @brief Initialises and returns the transport BDataIO for a print job.
 *
 * Called by the print server when a job is about to be sent through this
 * transport. If \a msg is NULL or another transport instance is already active,
 * NULL is returned. Otherwise the "printer_file" string field is read from
 * \a msg, the corresponding printer directory is opened, and
 * instantiate_transport() is called to create the concrete BDataIO object.
 *
 * @param msg  BMessage containing at minimum a "printer_file" string field
 *             with the path to the printer's spool directory.
 * @return A non-NULL BDataIO pointer on success, or NULL on any failure.
 */
extern "C" _EXPORT BDataIO *init_transport(BMessage *msg)
{
	if (msg == NULL || gTransport != NULL)
		return NULL;

	const char *spool_path = msg->FindString("printer_file");

	if (spool_path && *spool_path != '\0') {
		BDirectory printer(spool_path);

		if (printer.InitCheck() == B_OK) {
			gTransport = instantiate_transport(&printer, msg);
			return gTransport;
		};
	};

	return NULL;
}

/**
 * @brief Shuts down and releases the active transport instance.
 *
 * Deletes the BDataIO object created by init_transport() and resets the
 * global pointer to NULL, allowing a subsequent call to init_transport() to
 * succeed.
 */
extern "C" _EXPORT void exit_transport()
{
	if (gTransport) {
		delete gTransport;
		gTransport = NULL;
	}
}
