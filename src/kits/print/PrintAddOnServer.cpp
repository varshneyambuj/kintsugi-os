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
 * @file PrintAddOnServer.cpp
 * @brief Messenger-based proxy for the print add-on server process.
 *
 * PrintAddOnServer launches the print add-on server application on construction,
 * provides typed request methods (AddPrinter, ConfigPage, ConfigJob, etc.) that
 * encode their arguments into BMessages and deliver them via a BMessenger, and
 * quits the server when destroyed.
 *
 * @see PrintAddOnServerProtocol.h, PrinterDriverAddOn
 */


#include "PrintAddOnServer.h"

#include <Entry.h>
#include <Roster.h>

#include "PrinterDriverAddOn.h"
#include "PrintAddOnServerProtocol.h"

/** @brief One second expressed in microseconds. */
static const bigtime_t kSeconds = 1000000L;

/** @brief Timeout for delivering a request message to the add-on server. */
static const bigtime_t kDeliveryTimeout = 30 * kSeconds;

/** @brief Timeout for waiting for a reply from the add-on server (infinite). */
static const bigtime_t kReplyTimeout = B_INFINITE_TIMEOUT;


/**
 * @brief Constructs a PrintAddOnServer and launches the add-on server for \a driver.
 *
 * Attempts to launch the print add-on server application; the launch status is
 * stored in fLaunchStatus and checked by all subsequent request methods.
 *
 * @param driver  Name of the printer driver add-on to pass in subsequent requests.
 */
PrintAddOnServer::PrintAddOnServer(const char* driver)
	:
	fDriver(driver)
{
	fLaunchStatus = Launch(fMessenger);
}


/**
 * @brief Destroys the PrintAddOnServer and sends a quit request to the server.
 *
 * If the server was launched successfully, a B_QUIT_REQUESTED message is sent
 * before the object is destroyed.
 */
PrintAddOnServer::~PrintAddOnServer()
{
	if (fLaunchStatus == B_OK)
		Quit();
}


/**
 * @brief Requests the add-on server to register a new printer.
 *
 * Sends a kMessageAddPrinter request containing the driver name and
 * \a spoolFolderName, then returns the status code from the reply.
 *
 * @param spoolFolderName  Leaf name of the spool directory for the new printer.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrintAddOnServer::AddPrinter(const char* spoolFolderName)
{
	BMessage message(kMessageAddPrinter);
	message.AddString(kPrinterDriverAttribute, Driver());
	message.AddString(kPrinterNameAttribute, spoolFolderName);

	BMessage reply;
	status_t result = SendRequest(message, reply);
	if (result != B_OK)
		return result;

	return GetResult(reply);
}


/**
 * @brief Requests the add-on server to run the page-setup dialog.
 *
 * Encodes the driver name, spool folder path, and current settings into a
 * kMessageConfigPage request. On success, \a settings is updated with any
 * values returned by the server.
 *
 * @param spoolFolder  Directory representing the printer's spool folder.
 * @param settings     In/out settings message; updated with server response.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrintAddOnServer::ConfigPage(BDirectory* spoolFolder,
	BMessage* settings)
{
	BMessage message(kMessageConfigPage);
	message.AddString(kPrinterDriverAttribute, Driver());
	AddDirectory(message, kPrinterFolderAttribute, spoolFolder);
	message.AddMessage(kPrintSettingsAttribute, settings);

	BMessage reply;
	status_t result = SendRequest(message, reply);
	if (result != B_OK)
		return result;

	return GetResultAndUpdateSettings(reply, settings);
}


/**
 * @brief Requests the add-on server to run the job-setup dialog.
 *
 * Encodes the driver name, spool folder path, and current settings into a
 * kMessageConfigJob request. On success, \a settings is updated with any
 * values returned by the server.
 *
 * @param spoolFolder  Directory representing the printer's spool folder.
 * @param settings     In/out settings message; updated with server response.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrintAddOnServer::ConfigJob(BDirectory* spoolFolder,
	BMessage* settings)
{
	BMessage message(kMessageConfigJob);
	message.AddString(kPrinterDriverAttribute, Driver());
	AddDirectory(message, kPrinterFolderAttribute, spoolFolder);
	message.AddMessage(kPrintSettingsAttribute, settings);

	BMessage reply;
	status_t result = SendRequest(message, reply);
	if (result != B_OK)
		return result;

	return GetResultAndUpdateSettings(reply, settings);
}


/**
 * @brief Requests the add-on server to return default print settings.
 *
 * Sends a kMessageDefaultSettings request and populates \a settings with
 * the defaults returned by the driver.
 *
 * @param spoolFolder  Directory representing the printer's spool folder.
 * @param settings     Receives the default settings on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrintAddOnServer::DefaultSettings(BDirectory* spoolFolder,
	BMessage* settings)
{
	BMessage message(kMessageDefaultSettings);
	message.AddString(kPrinterDriverAttribute, Driver());
	AddDirectory(message, kPrinterFolderAttribute, spoolFolder);

	BMessage reply;
	status_t result = SendRequest(message, reply);
	if (result != B_OK)
		return result;

	return GetResultAndUpdateSettings(reply, settings);
}


/**
 * @brief Requests the add-on server to process (print) a spool job file.
 *
 * Sends a kMessageTakeJob request containing the driver name, spool file
 * path, and spool folder path.
 *
 * @param spoolFile    Full path to the spool job file to print.
 * @param spoolFolder  Directory representing the printer's spool folder.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrintAddOnServer::TakeJob(const char* spoolFile,
				BDirectory* spoolFolder)
{
	BMessage message(kMessageTakeJob);
	message.AddString(kPrinterDriverAttribute, Driver());
	message.AddString(kPrintJobFileAttribute, spoolFile);
	AddDirectory(message, kPrinterFolderAttribute, spoolFolder);

	BMessage reply;
	status_t result = SendRequest(message, reply);
	if (result != B_OK)
		return result;

	return GetResult(reply);
}


/**
 * @brief Finds the filesystem path to a named printer driver add-on.
 *
 * Delegates directly to PrinterDriverAddOn::FindPathToDriver().
 *
 * @param driver  Name of the driver to locate.
 * @param path    Receives the full path on success.
 * @return B_OK if the driver was found, or an error code otherwise.
 */
status_t
PrintAddOnServer::FindPathToDriver(const char* driver, BPath* path)
{
	return PrinterDriverAddOn::FindPathToDriver(driver, path);
}


/**
 * @brief Returns the printer driver name passed at construction time.
 *
 * @return Null-terminated driver name string.
 */
const char*
PrintAddOnServer::Driver() const
{
	return fDriver.String();
}


/**
 * @brief Launches the print add-on server application and obtains a messenger.
 *
 * Uses be_roster to start the server by its application signature and constructs
 * a BMessenger targeting the newly launched team.
 *
 * @param messenger  Receives a valid BMessenger to the launched server on success.
 * @return B_OK if the server was launched, or an error code on failure.
 */
status_t
PrintAddOnServer::Launch(BMessenger& messenger)
{
	team_id team;
	status_t result =
		be_roster->Launch(kPrintAddOnServerApplicationSignature,
			(BMessage*)NULL, &team);
	if (result != B_OK) {
		return result;
	}

	fMessenger = BMessenger(kPrintAddOnServerApplicationSignature, team);
	return result;
}


/**
 * @brief Returns true if the add-on server was launched successfully.
 *
 * @return true if fLaunchStatus is B_OK, false otherwise.
 */
bool
PrintAddOnServer::IsLaunched()
{
	return fLaunchStatus == B_OK;
}


/**
 * @brief Sends a quit request to the add-on server and marks it as stopped.
 *
 * Posts B_QUIT_REQUESTED to the server messenger and sets fLaunchStatus to
 * B_ERROR so that subsequent calls detect the server is no longer running.
 */
void
PrintAddOnServer::Quit()
{
	if (fLaunchStatus == B_OK) {
		fMessenger.SendMessage(B_QUIT_REQUESTED);
		fLaunchStatus = B_ERROR;
	}
}


/**
 * @brief Serialises a BDirectory as its filesystem path into a BMessage field.
 *
 * Resolves \a directory to a BPath and adds the path string to \a message
 * under \a name. Does nothing if the path cannot be resolved.
 *
 * @param message    The BMessage to add the path string to.
 * @param name       Field name to store the path under.
 * @param directory  The directory whose path is to be serialised.
 */
void
PrintAddOnServer::AddDirectory(BMessage& message, const char* name,
	BDirectory* directory)
{
	BEntry entry;
	status_t result = directory->GetEntry(&entry);
	if (result != B_OK)
		return;

	BPath path;
	if (entry.GetPath(&path) != B_OK)
		return;

	message.AddString(name, path.Path());
}


/**
 * @brief Serialises an entry_ref as its filesystem path into a BMessage field.
 *
 * Constructs a BPath from \a entryRef and adds the path string to \a message
 * under \a name. Does nothing if the path cannot be resolved.
 *
 * @param message   The BMessage to add the path string to.
 * @param name      Field name to store the path under.
 * @param entryRef  The entry_ref whose path is to be serialised.
 */
void
PrintAddOnServer::AddEntryRef(BMessage& message, const char* name,
	const entry_ref* entryRef)
{
	BPath path(entryRef);
	if (path.InitCheck() != B_OK)
		return;

	message.AddString(name, path.Path());
}


/**
 * @brief Sends a request to the add-on server and waits for a reply.
 *
 * Checks that the server is still running, then calls
 * BMessenger::SendMessage() with the configured delivery and reply timeouts.
 *
 * @param request  The request BMessage to send.
 * @param reply    Receives the server's reply message.
 * @return B_OK on success, or the launch/send error code on failure.
 */
status_t
PrintAddOnServer::SendRequest(BMessage& request, BMessage& reply)
{
	if (!IsLaunched())
		return fLaunchStatus;

	return fMessenger.SendMessage(&request, &reply, kDeliveryTimeout,
		kReplyTimeout);
}


/**
 * @brief Extracts the integer status code from an add-on server reply.
 *
 * Reads the kPrintAddOnServerStatusAttribute int32 field from \a reply and
 * returns it as a status_t.
 *
 * @param reply  The reply BMessage returned by the server.
 * @return The status code from the reply, or an error if the field is missing.
 */
status_t
PrintAddOnServer::GetResult(BMessage& reply)
{
	int32 status;
	status_t result = reply.FindInt32(kPrintAddOnServerStatusAttribute,
		&status);
	if (result != B_OK)
		return result;
	return static_cast<status_t>(status);
}


/**
 * @brief Extracts settings from a server reply and updates the caller's message.
 *
 * If the reply contains a kPrintSettingsAttribute sub-message, it is copied
 * into \a settings. The integer status code is then extracted and returned.
 *
 * @param reply     The reply BMessage returned by the server.
 * @param settings  The settings message to update in-place.
 * @return The status code from the reply.
 */
status_t
PrintAddOnServer::GetResultAndUpdateSettings(BMessage& reply, BMessage* settings)
{
	BMessage newSettings;
	if (reply.FindMessage(kPrintSettingsAttribute, &newSettings) == B_OK)
		*settings = newSettings;

	settings->PrintToStream();

	return GetResult(reply);
}
