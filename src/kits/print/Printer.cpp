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
 * @file Printer.cpp
 * @brief BPrinter — filesystem-backed printer descriptor for the print kit.
 *
 * BPrinter wraps a printer's spool directory entry_ref, providing accessors
 * for printer attributes (name, driver, transport, state) stored as node
 * attributes. It also manages a BMessenger-based node watcher so callers can
 * receive notifications when printer state changes.
 *
 * @see BPrinterRoster, BJobSetupPanel
 */


#include <Printer.h>

#include <FindDirectory.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>


#include <new>


namespace BPrivate {
	namespace Print {


// TODO: remove, after pr_server.h cleanup

// mime file types
#define PSRV_PRINTER_MIMETYPE					"application/x-vnd.Be.printer"


// printer attributes
#define PSRV_PRINTER_ATTR_STATE					"state"
#define PSRV_PRINTER_ATTR_COMMENTS				"Comments"
#define PSRV_PRINTER_ATTR_TRANSPORT				"transport"
#define PSRV_PRINTER_ATTR_DRIVER_NAME			"Driver Name"
#define PSRV_PRINTER_ATTR_PRINTER_NAME			"Printer Name"
#define PSRV_PRINTER_ATTR_DEFAULT_PRINTER		"Default Printer"
#define PSRV_PRINTER_ATTR_TRANSPORT_ADDRESS		"transport_address"


// message fields
#define PSRV_FIELD_CURRENT_PRINTER				"current_printer"


/**
 * @brief Constructs an empty, invalid BPrinter.
 *
 * The printer's entry_ref is zeroed; IsValid() will return false until
 * SetTo() is called with a valid printer directory.
 */
BPrinter::BPrinter()
	: fListener(NULL)
{
	memset(&fPrinterEntryRef, 0, sizeof(entry_ref));
}


/**
 * @brief Constructs a BPrinter pointing to the printer described by \a entry.
 *
 * @param entry  BEntry of the printer's spool directory.
 */
BPrinter::BPrinter(const BEntry& entry)
	: fListener(NULL)
{
	SetTo(entry);
}


/**
 * @brief Copy-constructs a BPrinter, duplicating the watcher if active.
 *
 * @param printer  The BPrinter to copy.
 */
BPrinter::BPrinter(const BPrinter& printer)
{
	*this = printer;
}


/**
 * @brief Constructs a BPrinter from a node reference.
 *
 * Resolves the node_ref to a BDirectory and delegates to SetTo(BDirectory).
 *
 * @param nodeRef  The node_ref of the printer's spool directory.
 */
BPrinter::BPrinter(const node_ref& nodeRef)
	: fListener(NULL)
{
	SetTo(nodeRef);
}


/**
 * @brief Constructs a BPrinter from an entry reference.
 *
 * @param entryRef  The entry_ref of the printer's spool directory entry.
 */
BPrinter::BPrinter(const entry_ref& entryRef)
	: fListener(NULL)
	, fPrinterEntryRef(entryRef)
{
}


/**
 * @brief Constructs a BPrinter from a BDirectory.
 *
 * @param directory  The printer's spool directory.
 */
BPrinter::BPrinter(const BDirectory& directory)
	: fListener(NULL)
{
	SetTo(directory);
}


/**
 * @brief Destroys the BPrinter and stops any active node watcher.
 */
BPrinter::~BPrinter()
{
	StopWatching();
}


/**
 * @brief Points this BPrinter at the printer described by \a entry.
 *
 * Stops any active watcher, stores the entry ref, and returns the result
 * of InitCheck().
 *
 * @param entry  BEntry of the printer's spool directory.
 * @return B_OK if the entry is a valid directory, or an error code otherwise.
 */
status_t
BPrinter::SetTo(const BEntry& entry)
{
	StopWatching();
	entry.GetRef(&fPrinterEntryRef);

	return InitCheck();
}


/**
 * @brief Points this BPrinter at the printer identified by \a nodeRef.
 *
 * @param nodeRef  The node_ref of the printer's spool directory.
 * @return B_OK on success, or an error code otherwise.
 */
status_t
BPrinter::SetTo(const node_ref& nodeRef)
{
	SetTo(BDirectory(&nodeRef));
	return InitCheck();
}


/**
 * @brief Points this BPrinter at the printer identified by \a entryRef.
 *
 * @param entryRef  The entry_ref of the printer's spool directory entry.
 * @return B_OK on success, or an error code otherwise.
 */
status_t
BPrinter::SetTo(const entry_ref& entryRef)
{
	StopWatching();
	fPrinterEntryRef = entryRef;

	return InitCheck();
}


/**
 * @brief Points this BPrinter at the printer described by \a directory.
 *
 * @param directory  The printer's spool directory.
 * @return B_OK on success, or an error code otherwise.
 */
status_t
BPrinter::SetTo(const BDirectory& directory)
{
	StopWatching();

	BEntry entry;
	directory.GetEntry(&entry);
	entry.GetRef(&fPrinterEntryRef);

	return InitCheck();
}


/**
 * @brief Resets this BPrinter to an empty, invalid state and stops watching.
 */
void
BPrinter::Unset()
{
	StopWatching();
	memset(&fPrinterEntryRef, 0, sizeof(entry_ref));
}


/**
 * @brief Returns true if this object represents a properly configured printer.
 *
 * Checks that the entry_ref opens as a BDirectory with the correct printer
 * MIME type attribute.
 *
 * @return true if the printer is valid, false otherwise.
 */
bool
BPrinter::IsValid() const
{
	BDirectory spoolDir(&fPrinterEntryRef);
	if (spoolDir.InitCheck() != B_OK)
		return false;

	BNode node(spoolDir);
	char type[B_MIME_TYPE_LENGTH];
	BNodeInfo(&node).GetType(type);

	if (strcmp(type, PSRV_PRINTER_MIMETYPE) != 0)
		return false;

	return true;
}


/**
 * @brief Returns the initialisation status of the underlying spool directory.
 *
 * @return B_OK if the entry_ref resolves to a readable directory.
 */
status_t
BPrinter::InitCheck() const
{
	BDirectory spoolDir(&fPrinterEntryRef);
	return spoolDir.InitCheck();
}


/**
 * @brief Returns true if the printer's state attribute indicates it is free.
 *
 * @return true if State() == "free".
 */
bool
BPrinter::IsFree() const
{
	return (State() == "free");
}


/**
 * @brief Returns true if this printer is the system default.
 *
 * Reads the PSRV_PRINTER_ATTR_DEFAULT_PRINTER bool attribute from the spool
 * directory.
 *
 * @return true if the default-printer attribute is set to true.
 */
bool
BPrinter::IsDefault() const
{
	bool isDefault = false;

	BDirectory spoolDir(&fPrinterEntryRef);
	if (spoolDir.InitCheck() == B_OK)
		spoolDir.ReadAttr(PSRV_PRINTER_ATTR_DEFAULT_PRINTER, B_BOOL_TYPE, 0,
			&isDefault, sizeof(bool));

	return isDefault;
}


/**
 * @brief Returns true if the printer can be shared over the network.
 *
 * Currently only the "Preview" virtual printer reports as shareable.
 *
 * @return true if the printer supports network sharing.
 */
bool
BPrinter::IsShareable() const
{
	if (Name() == "Preview")
		return true;

	return false;
}


/**
 * @brief Returns the human-readable name of this printer.
 *
 * @return The value of the PSRV_PRINTER_ATTR_PRINTER_NAME node attribute.
 */
BString
BPrinter::Name() const
{
	return _ReadAttribute(PSRV_PRINTER_ATTR_PRINTER_NAME);
}


/**
 * @brief Returns the current operational state string of this printer.
 *
 * @return The value of the PSRV_PRINTER_ATTR_STATE node attribute (e.g. "free").
 */
BString
BPrinter::State() const
{
	return _ReadAttribute(PSRV_PRINTER_ATTR_STATE);
}


/**
 * @brief Returns the name of the printer driver add-on for this printer.
 *
 * @return The value of the PSRV_PRINTER_ATTR_DRIVER_NAME node attribute.
 */
BString
BPrinter::Driver() const
{
	return _ReadAttribute(PSRV_PRINTER_ATTR_DRIVER_NAME);
}


/**
 * @brief Returns the comments string associated with this printer.
 *
 * @return The value of the PSRV_PRINTER_ATTR_COMMENTS node attribute.
 */
BString
BPrinter::Comments() const
{
	return _ReadAttribute(PSRV_PRINTER_ATTR_COMMENTS);
}


/**
 * @brief Returns the transport add-on name for this printer.
 *
 * @return The value of the PSRV_PRINTER_ATTR_TRANSPORT node attribute.
 */
BString
BPrinter::Transport() const
{
	return _ReadAttribute(PSRV_PRINTER_ATTR_TRANSPORT);
}


/**
 * @brief Returns the transport address string for this printer.
 *
 * @return The value of the PSRV_PRINTER_ATTR_TRANSPORT_ADDRESS node attribute.
 */
BString
BPrinter::TransportAddress() const
{
	return _ReadAttribute(PSRV_PRINTER_ATTR_TRANSPORT_ADDRESS);
}


/**
 * @brief Loads the driver add-on and retrieves the default print settings.
 *
 * Dynamically loads the driver add-on, resolves the "default_settings" symbol,
 * and calls it with the printer's BNode. The resulting BMessage is copied into
 * \a settings and the printer name is appended.
 *
 * @param settings  Receives the default settings on success.
 * @return B_OK on success, or an error code if the driver cannot be loaded or
 *         does not export the required symbol.
 */
status_t
BPrinter::DefaultSettings(BMessage& settings)
{
	status_t status = B_ERROR;
	image_id id = _LoadDriver();
	if (id < 0)
		return status;

	typedef BMessage* (*default_settings_func_t)(BNode*);
	default_settings_func_t default_settings;
	if (get_image_symbol(id, "default_settings", B_SYMBOL_TYPE_TEXT
		, (void**)&default_settings) == B_OK) {
		BNode printerNode(&fPrinterEntryRef);
		BMessage *newSettings = default_settings(&printerNode);
		if (newSettings) {
			status = B_OK;
			settings = *newSettings;
			_AddPrinterName(settings);
		}
		delete newSettings;
	}
	unload_add_on(id);
	return status;
}


/**
 * @brief Starts watching the printer's parent directory for changes.
 *
 * Stores a copy of \a listener and registers B_WATCH_DIRECTORY monitoring
 * on the directory containing the printer's spool folder.
 *
 * @param listener  A valid BMessenger to receive node-monitor notifications.
 * @return B_OK on success, B_BAD_VALUE if the messenger is invalid, or
 *         B_NO_MEMORY if the messenger copy cannot be allocated.
 */
status_t
BPrinter::StartWatching(const BMessenger& listener)
{
	StopWatching();

	if (!listener.IsValid())
		return B_BAD_VALUE;

	fListener = new(std::nothrow) BMessenger(listener);
	if (!fListener)
		return B_NO_MEMORY;

	node_ref nodeRef;
	nodeRef.device = fPrinterEntryRef.device;
	nodeRef.node = fPrinterEntryRef.directory;

	return watch_node(&nodeRef, B_WATCH_DIRECTORY, *fListener);
}


/**
 * @brief Stops watching the printer's parent directory for changes.
 *
 * Cancels the node-monitor registration and frees the listener messenger.
 */
void
BPrinter::StopWatching()
{
	if (fListener) {
		stop_watching(*fListener);
		delete fListener;
		fListener = NULL;
	}
}


/**
 * @brief Copy-assigns another BPrinter to this instance.
 *
 * Resets the current state, copies the entry_ref, and restarts the watcher
 * if the source printer had one active.
 *
 * @param printer  The source BPrinter to copy.
 * @return A reference to this object.
 */
BPrinter&
BPrinter::operator=(const BPrinter& printer)
{
	if (this != &printer) {
		Unset();
		fPrinterEntryRef = printer.fPrinterEntryRef;
		if (printer.fListener)
			StartWatching(*printer.fListener);
	}
	return *this;
}


/**
 * @brief Returns true if two BPrinter objects refer to the same spool directory.
 *
 * @param printer  The BPrinter to compare against.
 * @return true if both entry_refs are equal.
 */
bool
BPrinter::operator==(const BPrinter& printer) const
{
	return (fPrinterEntryRef == printer.fPrinterEntryRef);
}


/**
 * @brief Returns true if two BPrinter objects refer to different spool directories.
 *
 * @param printer  The BPrinter to compare against.
 * @return true if the entry_refs differ.
 */
bool
BPrinter::operator!=(const BPrinter& printer) const
{
	return (fPrinterEntryRef != printer.fPrinterEntryRef);
}


/**
 * @brief Invokes the driver's "add_printer" entry point to configure the printer.
 *
 * Loads the driver add-on, resolves the "add_printer" symbol, and calls it
 * with the printer's name attribute.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPrinter::_Configure() const
{
	status_t status = B_ERROR;
	image_id id = _LoadDriver();
	if (id < 0)
		return status;

	BString printerName(_ReadAttribute(PSRV_PRINTER_ATTR_PRINTER_NAME));
	if (printerName.Length() > 0) {
		typedef char* (*add_printer_func_t)(const char*);
		add_printer_func_t add_printer;
		if (get_image_symbol(id, "add_printer", B_SYMBOL_TYPE_TEXT
			, (void**)&add_printer) == B_OK) {
				if (add_printer(printerName.String()) != NULL)
					status = B_OK;
		}
	} else {
		status = B_ERROR;
	}
	unload_add_on(id);
	return status;
}


/**
 * @brief Invokes the driver's "config_job" entry point to configure a print job.
 *
 * Loads the driver add-on, resolves "config_job", and calls it with the printer
 * node and current settings. On success, \a settings is replaced with the new
 * driver-provided settings and the printer name is appended.
 *
 * @param settings  In/out settings message; updated on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPrinter::_ConfigureJob(BMessage& settings)
{
	status_t status = B_ERROR;
	image_id id = _LoadDriver();
	if (id < 0)
		return status;

	typedef BMessage* (*config_job_func_t)(BNode*, const BMessage*);
	config_job_func_t configure_job;
	if (get_image_symbol(id, "config_job", B_SYMBOL_TYPE_TEXT
		, (void**)&configure_job) == B_OK) {
		BNode printerNode(&fPrinterEntryRef);
		BMessage *newSettings = configure_job(&printerNode, &settings);
		if (newSettings && (newSettings->what == 'okok')) {
			status = B_OK;
			settings = *newSettings;
			_AddPrinterName(settings);
		}
		delete newSettings;
	}
	unload_add_on(id);
	return status;
}


/**
 * @brief Invokes the driver's "config_page" entry point to configure page layout.
 *
 * Loads the driver add-on, resolves "config_page", and calls it with the printer
 * node and current settings. On success, \a settings is updated with the new
 * driver-provided page settings.
 *
 * @param settings  In/out settings message; updated on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPrinter::_ConfigurePage(BMessage& settings)
{
	status_t status = B_ERROR;
	image_id id = _LoadDriver();
	if (id < 0)
		return status;

	typedef BMessage* (*config_page_func_t)(BNode*, const BMessage*);
	config_page_func_t configure_page;
	if (get_image_symbol(id, "config_page", B_SYMBOL_TYPE_TEXT
		, (void**)&configure_page) == B_OK) {
		BNode printerNode(&fPrinterEntryRef);
		BMessage *newSettings = configure_page(&printerNode, &settings);
		if (newSettings && (newSettings->what == 'okok')) {
			status = B_OK;
			settings = *newSettings;
			_AddPrinterName(settings);
		}
		delete newSettings;
	}
	unload_add_on(id);
	return status;
}


/**
 * @brief Resolves the filesystem path to this printer's driver add-on.
 *
 * Reads the driver name attribute and searches the standard add-on directories
 * (user non-packaged, user, system non-packaged, system) under "Print/".
 *
 * @return A BPath to the driver add-on file, or an invalid BPath if not found.
 */
BPath
BPrinter::_DriverPath() const
{
	BString driverName(_ReadAttribute(PSRV_PRINTER_ATTR_DRIVER_NAME));
	if (driverName.Length() <= 0)
		return BPath();

	directory_which directories[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY
	};

	BPath path;
	driverName.Prepend("Print/");
	for (int32 i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
		if (find_directory(directories[i], &path) == B_OK) {
			path.Append(driverName.String());

			BEntry driver(path.Path());
			if (driver.InitCheck() == B_OK && driver.Exists() && driver.IsFile())
				return path;
		}
	}
	return BPath();
}


/**
 * @brief Loads the printer driver add-on into memory.
 *
 * Resolves the driver path via _DriverPath() and calls load_add_on().
 *
 * @return A valid image_id on success, or -1 if the driver cannot be found or loaded.
 */
image_id
BPrinter::_LoadDriver() const
{
	BPath driverPath(_DriverPath());
	if (driverPath.InitCheck() != B_OK)
		return -1;

	return load_add_on(driverPath.Path());
}


/**
 * @brief Removes and re-adds the current printer name to a settings message.
 *
 * Ensures the PSRV_FIELD_CURRENT_PRINTER field in \a settings reflects the
 * printer's current name attribute, replacing any stale value.
 *
 * @param settings  The BMessage to update in-place.
 */
void
BPrinter::_AddPrinterName(BMessage& settings)
{
	settings.RemoveName(PSRV_FIELD_CURRENT_PRINTER);
	settings.AddString(PSRV_FIELD_CURRENT_PRINTER, Name());
}


/**
 * @brief Reads a named string attribute from the printer's spool directory.
 *
 * Opens the spool directory and calls ReadAttrString() for the given attribute.
 * Returns an empty BString if the directory cannot be opened or the attribute
 * does not exist.
 *
 * @param attribute  Name of the node attribute to read.
 * @return The attribute value, or an empty BString on failure.
 */
BString
BPrinter::_ReadAttribute(const char* attribute) const
{
	BString value;

	BDirectory spoolDir(&fPrinterEntryRef);
	if (spoolDir.InitCheck() == B_OK)
		spoolDir.ReadAttrString(attribute, &value);

	return value;
}


	}	// namespace Print
}	// namespace BPrivate
