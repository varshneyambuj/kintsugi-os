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
 *   Authors: Ithamar R. Adema, Michael Pfeiffer
 */


/**
 * @file PrinterDriverAddOn.cpp
 * @brief Direct add-on loader and caller for printer driver entry points.
 *
 * PrinterDriverAddOn loads a named printer driver add-on from the standard
 * search directories at construction time and provides strongly typed wrappers
 * around each of the four conventional driver entry points: add_printer,
 * config_page, config_job, default_settings, and take_job. This class is the
 * in-process counterpart to PrintAddOnServer, which delegates to a separate
 * server process.
 *
 * @see PrintAddOnServer, BeUtils
 */


#include "PrinterDriverAddOn.h"

#include <File.h>

#include "BeUtils.h"
#include "pr_server.h"


typedef BMessage* (*config_func_t)(BNode*, const BMessage*);
typedef BMessage* (*take_job_func_t)(BFile*, BNode*, const BMessage*);
typedef char* (*add_printer_func_t)(const char* printer_name);
typedef BMessage* (*default_settings_t)(BNode*);

/** @brief Sub-directory name within add-on directories that holds printer drivers. */
static const char* kPrinterDriverFolderName = "Print";


/**
 * @brief Constructs a PrinterDriverAddOn and loads the named driver.
 *
 * Searches the standard add-on directory hierarchy for a driver named \a driver
 * under the "Print" subdirectory and calls load_add_on() if found.
 * fAddOnID is set to -1 on failure; callers should check IsLoaded().
 *
 * @param driver  Leaf name of the printer driver add-on to load.
 */
PrinterDriverAddOn::PrinterDriverAddOn(const char* driver)
	:
	fAddOnID(-1)
{
	BPath path;
	status_t result;
	result = FindPathToDriver(driver, &path);
	if (result != B_OK)
		return;

	fAddOnID = ::load_add_on(path.Path());
}


/**
 * @brief Destroys the PrinterDriverAddOn and unloads the driver if loaded.
 */
PrinterDriverAddOn::~PrinterDriverAddOn()
{
	if (IsLoaded()) {
		unload_add_on(fAddOnID);
		fAddOnID = -1;
	}
}


/**
 * @brief Calls the driver's "add_printer" entry point to register a new printer.
 *
 * Resolves the "add_printer" symbol and invokes it with \a spoolFolderName.
 *
 * @param spoolFolderName  Leaf name of the spool directory to create.
 * @return B_OK if the driver accepted the name, or B_ERROR on failure.
 */
status_t
PrinterDriverAddOn::AddPrinter(const char* spoolFolderName)
{
	if (!IsLoaded())
		return B_ERROR;

	add_printer_func_t func;
	status_t result = get_image_symbol(fAddOnID, "add_printer",
		B_SYMBOL_TYPE_TEXT, (void**)&func);
	if (result != B_OK)
		return result;

	if ((*func)(spoolFolderName) == NULL)
		return B_ERROR;
	return B_OK;
}


/**
 * @brief Calls the driver's "config_page" entry point to configure page layout.
 *
 * Invokes the "config_page" driver symbol with \a spoolFolder and \a settings.
 * On success, \a settings is updated with the driver's new settings via
 * CopyValidSettings().
 *
 * @param spoolFolder  Pointer to the printer's spool directory.
 * @param settings     In/out settings message; updated on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrinterDriverAddOn::ConfigPage(BDirectory* spoolFolder, BMessage* settings)
{
	if (!IsLoaded())
		return B_ERROR;

	config_func_t func;
	status_t result = get_image_symbol(fAddOnID, "config_page",
		B_SYMBOL_TYPE_TEXT, (void**)&func);
	if (result != B_OK)
		return result;

	BMessage* newSettings = (*func)(spoolFolder, settings);
	result = CopyValidSettings(settings, newSettings);
	delete newSettings;

	return result;
}


/**
 * @brief Calls the driver's "config_job" entry point to configure a print job.
 *
 * Invokes the "config_job" driver symbol with \a spoolFolder and \a settings.
 * On success, \a settings is updated with the driver's new settings via
 * CopyValidSettings().
 *
 * @param spoolFolder  Pointer to the printer's spool directory.
 * @param settings     In/out settings message; updated on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrinterDriverAddOn::ConfigJob(BDirectory* spoolFolder, BMessage* settings)
{
	if (!IsLoaded())
		return B_ERROR;

	config_func_t func;
	status_t result = get_image_symbol(fAddOnID, "config_job",
		B_SYMBOL_TYPE_TEXT, (void**)&func);
	if (result != B_OK)
		return result;

	BMessage* newSettings = (*func)(spoolFolder, settings);
	result = CopyValidSettings(settings, newSettings);
	delete newSettings;

	return result;
}


/**
 * @brief Calls the driver's "default_settings" entry point.
 *
 * Invokes the "default_settings" driver symbol with \a spoolFolder. On success
 * \a settings receives the default values and its "what" field is set to 'okok'.
 *
 * @param spoolFolder  Pointer to the printer's spool directory.
 * @param settings     Receives the default settings on success.
 * @return B_OK on success, or B_ERROR if the driver returns NULL.
 */
status_t
PrinterDriverAddOn::DefaultSettings(BDirectory* spoolFolder, BMessage* settings)
{
	if (!IsLoaded())
		return B_ERROR;

	default_settings_t func;
	status_t result = get_image_symbol(fAddOnID, "default_settings",
		B_SYMBOL_TYPE_TEXT, (void**)&func);
	if (result != B_OK)
		return result;

	BMessage* newSettings = (*func)(spoolFolder);
	if (newSettings != NULL) {
		*settings = *newSettings;
		settings->what = 'okok';
	} else
		result = B_ERROR;
	delete newSettings;

	return result;
}


/**
 * @brief Calls the driver's "take_job" entry point to process a print job.
 *
 * Opens the spool file for reading and writing, resolves the "take_job" symbol,
 * and invokes it. A legacy BMessage containing file and printer pointers as
 * integers is also passed for compatibility with older add-ons.
 *
 * @param spoolFile    Full path to the spool job file.
 * @param spoolFolder  Pointer to the printer's spool directory.
 * @return B_OK if the driver reported success ('okok'), or B_ERROR otherwise.
 */
status_t
PrinterDriverAddOn::TakeJob(const char* spoolFile, BDirectory* spoolFolder)
{
	if (!IsLoaded())
		return B_ERROR;

	BFile file(spoolFile, B_READ_WRITE);
	take_job_func_t func;
	status_t result = get_image_symbol(fAddOnID, "take_job", B_SYMBOL_TYPE_TEXT,
		(void**)&func);
	if (result != B_OK)
		return result;

	// This seems to be required for legacy?
	// HP PCL3 add-on crashes without it!
	BMessage parameters(B_REFS_RECEIVED);
	parameters.AddInt32("file", (addr_t)&file);
	parameters.AddInt32("printer", (addr_t)spoolFolder);

	BMessage* message = (*func)(&file, spoolFolder, &parameters);
	if (message == NULL || message->what != 'okok')
		result = B_ERROR;
	delete message;

	return result;
}


/**
 * @brief Locates the filesystem path to a named printer driver add-on.
 *
 * Searches the four standard add-on directory tiers in priority order:
 * user non-packaged, user, system non-packaged, and system; each under the
 * "Print" subdirectory.
 *
 * @param driver  Leaf name of the printer driver to find.
 * @param path    Receives the full path to the driver on success.
 * @return B_OK if the driver was found, or an error code otherwise.
 */
status_t
PrinterDriverAddOn::FindPathToDriver(const char* driver, BPath* path)
{
	status_t result;
	result = ::TestForAddonExistence(driver,
		B_USER_NONPACKAGED_ADDONS_DIRECTORY, kPrinterDriverFolderName, *path);
	if (result == B_OK)
		return B_OK;

	result = ::TestForAddonExistence(driver, B_USER_ADDONS_DIRECTORY,
		kPrinterDriverFolderName, *path);
	if (result == B_OK)
		return B_OK;

	result = ::TestForAddonExistence(driver,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY, kPrinterDriverFolderName, *path);
	if (result == B_OK)
		return B_OK;

	result = ::TestForAddonExistence(driver, B_SYSTEM_ADDONS_DIRECTORY,
		kPrinterDriverFolderName, *path);
	return result;
}


/**
 * @brief Returns true if the driver add-on is currently loaded.
 *
 * @return true if fAddOnID is a positive (valid) image_id.
 */
bool
PrinterDriverAddOn::IsLoaded() const
{
	return fAddOnID > 0;
}


/**
 * @brief Copies valid settings from a driver reply into the caller's message.
 *
 * If \a newSettings is non-NULL and its "what" field is not 'baad', the
 * contents are copied into \a settings and its "what" is set to 'okok'.
 *
 * @param settings     The settings message to update.
 * @param newSettings  The driver-provided settings to copy, or NULL.
 * @return B_OK if the settings were valid and copied, B_ERROR otherwise.
 */
status_t
PrinterDriverAddOn::CopyValidSettings(BMessage* settings, BMessage* newSettings)
{
	if (newSettings != NULL && newSettings->what != 'baad') {
		*settings = *newSettings;
		settings->what = 'okok';
		return B_OK;
	}
	return B_ERROR;
}
