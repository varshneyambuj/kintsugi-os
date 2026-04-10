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
 *   Copyright 2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */

/** @file PrintAddOnServerApplication.cpp
 *  @brief Server application that delegates print operations to printer driver add-ons. */

#include "PrintAddOnServerApplication.h"

#include <PrinterDriverAddOn.h>

#include <String.h>

/**
 * @brief Constructs the print add-on server application.
 *
 * @param signature The application MIME signature.
 */
PrintAddOnServerApplication::PrintAddOnServerApplication(const char* signature)
	:
	BApplication(signature)
{

}


/**
 * @brief Dispatches incoming messages to the appropriate print operation handler.
 *
 * @param message The incoming message.
 */
void
PrintAddOnServerApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMessageAddPrinter:
			AddPrinter(message);
			break;

		case kMessageConfigPage:
			ConfigPage(message);
			break;

		case kMessageConfigJob:
			ConfigJob(message);
			break;

		case kMessageDefaultSettings:
			DefaultSettings(message);
			break;

		case kMessageTakeJob:
			TakeJob(message);
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


/**
 * @brief Handles an add-printer request from a BMessage.
 *
 * Extracts the driver and printer name, delegates to AddPrinter(), and sends a reply.
 *
 * @param message The request message.
 */
void
PrintAddOnServerApplication::AddPrinter(BMessage* message)
{
	BString driver;
	BString name;
	if (message->FindString(kPrinterDriverAttribute, &driver) != B_OK
			|| message->FindString(kPrinterNameAttribute, &name) != B_OK) {
		SendReply(message, B_BAD_VALUE);
		return;
	}

	status_t status = AddPrinter(driver.String(), name.String());
	SendReply(message, status);
}


/**
 * @brief Adds a printer by loading the driver add-on and calling its AddPrinter function.
 *
 * @param driver          The printer driver name.
 * @param spoolFolderName The spool folder name for the printer.
 * @return B_OK on success, or an error code on failure.
 */
status_t
PrintAddOnServerApplication::AddPrinter(const char* driver,
	const char* spoolFolderName)
{
	PrinterDriverAddOn addOn(driver);
	return addOn.AddPrinter(spoolFolderName);
}


/**
 * @brief Handles a page configuration request from a BMessage.
 *
 * @param message The request message containing driver, folder, and settings.
 */
void
PrintAddOnServerApplication::ConfigPage(BMessage* message)
{
	BString driver;
	BString folder;
	BMessage settings;
	if (message->FindString(kPrinterDriverAttribute, &driver) != B_OK
		|| message->FindString(kPrinterFolderAttribute, &folder) != B_OK
		|| message->FindMessage(kPrintSettingsAttribute, &settings) != B_OK) {
		SendReply(message, B_BAD_VALUE);
		return;
	}

	BDirectory spoolFolder(folder.String());
	status_t status = ConfigPage(driver.String(), &spoolFolder, &settings);

	if (status != B_OK)
		SendReply(message, status);
	else {
		BMessage reply(B_REPLY);
		reply.AddMessage(kPrintSettingsAttribute, &settings);
		SendReply(message, &reply);
	}
}


/**
 * @brief Configures page settings by loading the driver add-on.
 *
 * @param driver      The printer driver name.
 * @param spoolFolder The spool folder directory.
 * @param settings    In/out page settings message.
 * @return B_OK on success, or an error code.
 */
status_t
PrintAddOnServerApplication::ConfigPage(const char* driver,
	BDirectory* spoolFolder, BMessage* settings)
{
	PrinterDriverAddOn addOn(driver);
	return addOn.ConfigPage(spoolFolder, settings);
}


/**
 * @brief Handles a job configuration request from a BMessage.
 *
 * @param message The request message containing driver, folder, and settings.
 */
void
PrintAddOnServerApplication::ConfigJob(BMessage* message)
{
	BString driver;
	BString folder;
	BMessage settings;
	if (message->FindString(kPrinterDriverAttribute, &driver) != B_OK
		|| message->FindString(kPrinterFolderAttribute, &folder) != B_OK
		|| message->FindMessage(kPrintSettingsAttribute, &settings) != B_OK) {
		SendReply(message, B_BAD_VALUE);
		return;
	}

	BDirectory spoolFolder(folder.String());
	status_t status = ConfigJob(driver.String(), &spoolFolder, &settings);

	if (status != B_OK)
		SendReply(message, status);
	else {
		BMessage reply(B_REPLY);
		reply.AddMessage(kPrintSettingsAttribute, &settings);
		SendReply(message, &reply);
	}
}


/**
 * @brief Configures job settings by loading the driver add-on.
 *
 * @param driver      The printer driver name.
 * @param spoolFolder The spool folder directory.
 * @param settings    In/out job settings message.
 * @return B_OK on success, or an error code.
 */
status_t
PrintAddOnServerApplication::ConfigJob(const char* driver,
				BDirectory* spoolFolder,
				BMessage* settings)
{
	PrinterDriverAddOn addOn(driver);
	return addOn.ConfigJob(spoolFolder, settings);
}


/**
 * @brief Handles a default-settings request from a BMessage.
 *
 * @param message The request message containing driver and folder.
 */
void
PrintAddOnServerApplication::DefaultSettings(BMessage* message)
{
	BString driver;
	BString folder;
	if (message->FindString(kPrinterDriverAttribute, &driver) != B_OK
		|| message->FindString(kPrinterFolderAttribute, &folder) != B_OK) {
		SendReply(message, B_BAD_VALUE);
		return;
	}

	BMessage settings;
	BDirectory spoolFolder(folder.String());
	status_t status = DefaultSettings(driver.String(), &spoolFolder, &settings);

	if (status != B_OK)
		SendReply(message, status);
	else {
		BMessage reply(B_REPLY);
		reply.AddMessage(kPrintSettingsAttribute, &settings);
		SendReply(message, &reply);
	}
}


/**
 * @brief Retrieves default print settings from the driver add-on.
 *
 * @param driver      The printer driver name.
 * @param spoolFolder The spool folder directory.
 * @param settings    Output: the default settings message.
 * @return B_OK on success, or an error code.
 */
status_t
PrintAddOnServerApplication::DefaultSettings(const char* driver,
	BDirectory* spoolFolder, BMessage* settings)
{
	PrinterDriverAddOn addOn(driver);
	return addOn.DefaultSettings(spoolFolder, settings);
}


/**
 * @brief Handles a take-job request from a BMessage.
 *
 * @param message The request message containing driver, folder, and job file path.
 */
void
PrintAddOnServerApplication::TakeJob(BMessage* message)
{
	BString driver;
	BString folder;
	BString jobFile;
	if (message->FindString(kPrinterDriverAttribute, &driver) != B_OK
		|| message->FindString(kPrinterFolderAttribute, &folder) != B_OK
		|| message->FindString(kPrintJobFileAttribute, &jobFile) != B_OK) {
		SendReply(message, B_BAD_VALUE);
		return;
	}

	BDirectory spoolFolder(folder.String());
	status_t status = TakeJob(driver.String(), jobFile.String(),
		&spoolFolder);
	SendReply(message, status);
}


/**
 * @brief Sends a spool file to the printer driver add-on for processing.
 *
 * @param driver      The printer driver name.
 * @param spoolFile   Path to the spool file.
 * @param spoolFolder The spool folder directory.
 * @return B_OK on success, or an error code.
 */
status_t
PrintAddOnServerApplication::TakeJob(const char* driver, const char* spoolFile,
				BDirectory* spoolFolder)
{
	PrinterDriverAddOn addOn(driver);
	return addOn.TakeJob(spoolFile, spoolFolder);
}


/**
 * @brief Sends a status-only reply to the requesting message.
 *
 * @param message The original request message.
 * @param status  The status code to include in the reply.
 */
void
PrintAddOnServerApplication::SendReply(BMessage* message, status_t status)
{
	BMessage reply(B_REPLY);
	reply.AddInt32(kPrintAddOnServerStatusAttribute, status);
	message->SendReply(&reply);
}


/**
 * @brief Sends a reply message with B_OK status to the requesting message.
 *
 * @param message The original request message.
 * @param reply   The reply message to send.
 */
void
PrintAddOnServerApplication::SendReply(BMessage* message, BMessage* reply)
{
	reply->AddInt32(kPrintAddOnServerStatusAttribute, B_OK);
	message->SendReply(reply);
}


/**
 * @brief Application entry point for the print add-on server.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 */
int
main(int argc, char* argv[])
{
	PrintAddOnServerApplication application(
		kPrintAddOnServerApplicationSignature);
	application.Run();
}
