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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2009 Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       I.R. Adema
 *       Stefano Ceccherini (burton666@libero.it)
 *       Michael Pfeiffer
 *       julun <host.haiku@gmx.de>
 */


/**
 * @file PrintJob.cpp
 * @brief Implementation of BPrintJob, the interface to the print system
 *
 * BPrintJob manages the printing workflow: opening the page-setup and print dialogs,
 * iterating over pages, recording view drawing into print spools, and submitting jobs
 * to the print server.
 *
 * @see BView, BWindow
 */


#include <PrintJob.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Debug.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <OS.h>
#include <Path.h>
#include <Region.h>
#include <Roster.h>
#include <SystemCatalog.h>
#include <View.h>

#include <AutoDeleter.h>
#include <pr_server.h>
#include <ViewPrivate.h>

using BPrivate::gSystemCatalog;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrintJob"

#undef B_TRANSLATE
#define B_TRANSLATE(str) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK(str), "PrintJob")


/*!	Summary of spool file:

		|-----------------------------------|
		|         print_file_header         |
		|-----------------------------------|
		|    BMessage print_job_settings    |
		|-----------------------------------|
		|                                   |
		| ********** (first page) ********* |
		| *                               * |
		| *         _page_header_         * |
		| * ----------------------------- * |
		| * |---------------------------| * |
		| * |       BPoint where        | * |
		| * |       BRect bounds        | * |
		| * |       BPicture pic        | * |
		| * |---------------------------| * |
		| * |---------------------------| * |
		| * |       BPoint where        | * |
		| * |       BRect bounds        | * |
		| * |       BPicture pic        | * |
		| * |---------------------------| * |
		| ********************************* |
		|                                   |
		| ********* (second page) ********* |
		| *                               * |
		| *         _page_header_         * |
		| * ----------------------------- * |
		| * |---------------------------| * |
		| * |       BPoint where        | * |
		| * |       BRect bounds        | * |
		| * |       BPicture pic        | * |
		| * |---------------------------| * |
		| ********************************* |
		|-----------------------------------|

	BeOS R5 print_file_header.version is 1 << 16
	BeOS R5 print_file_header.first_page is -1

	each page can consist of a collection of picture structures
	remaining pages start at _page_header_.next_page of previous _page_header_

	See also: "How to Write a BeOS R5 Printer Driver" for description of spool
	file format: http://haiku-os.org/documents/dev/how_to_write_a_printer_driver
*/


struct _page_header_ {
	int32 number_of_pictures;
	off_t next_page;
	int32 reserved[10];
} _PACKED;


/** @brief Display a modal error alert with an OK button.
 *  @param message The error text to display in the alert body.
 */
static void
ShowError(const char* message)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Error"), message, B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


// #pragma mark -- PrintServerMessenger


namespace BPrivate {


/** @brief Helper that sends a synchronous request to the print server.
 *
 *  PrintServerMessenger wraps the asynchronous print-server protocol: it
 *  spawns a dedicated thread to send the message, blocks user input while
 *  waiting, and pumps the originating window's message loop so it stays
 *  responsive.
 *
 *  @see BPrintJob::ConfigPage()
 *  @see BPrintJob::ConfigJob()
 */
class PrintServerMessenger {
public:
							PrintServerMessenger(uint32 what, BMessage* input);
							~PrintServerMessenger();

			BMessage*		Request();
			status_t		SendRequest();

			void			SetResult(BMessage* result);
			BMessage*		Result() const { return fResult; }

	static	status_t		GetPrintServerMessenger(BMessenger& messenger);

private:
			void			RejectUserInput();
			void			AllowUserInput();
			void			DeleteSemaphore();
	static	status_t		MessengerThread(void* data);

			/** @brief Message code to send to the print server. */
			uint32			fWhat;
			/** @brief Optional input message merged into the request. */
			BMessage*		fInput;
			/** @brief Outgoing request message built from fWhat and fInput. */
			BMessage*		fRequest;
			/** @brief Reply received from the print server on success. */
			BMessage*		fResult;
			/** @brief Semaphore used to signal the calling thread when done. */
			sem_id			fThreadCompleted;
			/** @brief Hidden application-modal window that blocks user input. */
			BAlert*			fHiddenApplicationModalWindow;
};


}	// namespace BPrivate


using namespace BPrivate;


// #pragma mark -- BPrintJob


/** @brief Construct a BPrintJob with the given job name.
 *  @param jobName Human-readable name for the print job, written into the
 *                 spool file attributes and displayed by the print server.
 *
 *  Initialises all member pointers to NULL / sentinel values and allocates the
 *  per-page header structure. The job is not ready to print until ConfigPage()
 *  or SetSettings() has been called, and BeginJob() has been called
 *  successfully.
 *
 *  @see ConfigPage()
 *  @see ConfigJob()
 *  @see BeginJob()
 */
BPrintJob::BPrintJob(const char* jobName)
	:
	fPrintJobName(NULL),
	fSpoolFile(NULL),
	fError(B_NO_INIT),
	fSetupMessage(NULL),
	fDefaultSetupMessage(NULL),
	fAbort(0),
	fCurrentPageHeader(NULL)
{
	memset(&fSpoolFileHeader, 0, sizeof(print_file_header));

	if (jobName != NULL && jobName[0])
		fPrintJobName = strdup(jobName);

	fCurrentPageHeader = new _page_header_;
	if (fCurrentPageHeader != NULL)
		memset(fCurrentPageHeader, 0, sizeof(_page_header_));
}


/** @brief Destroy the BPrintJob.
 *
 *  Cancels any in-progress job (deleting the partial spool file) and releases
 *  all owned resources including the job name string, setup messages, and the
 *  page-header buffer.
 *
 *  @see CancelJob()
 */
BPrintJob::~BPrintJob()
{
	CancelJob();

	free(fPrintJobName);
	delete fSetupMessage;
	delete fDefaultSetupMessage;
	delete fCurrentPageHeader;
}


/** @brief Display the page-setup dialog and store the resulting settings.
 *
 *  Sends PSRV_SHOW_PAGE_SETUP to the print server, waits for the user to
 *  confirm, and stores the returned BMessage as the current setup. The paper
 *  rectangle, printable rectangle, and resolution fields are updated from the
 *  reply via _HandlePageSetup().
 *
 *  @return B_OK if the user confirmed, or an error code if the print server
 *          could not be contacted or the request failed.
 *  @see ConfigJob()
 *  @see PaperRect()
 *  @see PrintableRect()
 */
status_t
BPrintJob::ConfigPage()
{
	PrintServerMessenger messenger(PSRV_SHOW_PAGE_SETUP, fSetupMessage);
	status_t status = messenger.SendRequest();
	if (status != B_OK)
		return status;

	delete fSetupMessage;
	fSetupMessage = messenger.Result();
	_HandlePageSetup(fSetupMessage);

	return B_OK;
}


/** @brief Display the print-job dialog and store the resulting settings.
 *
 *  Sends PSRV_SHOW_PRINT_SETUP to the print server, waits for the user to
 *  confirm, and stores the returned BMessage as the current setup. The first
 *  and last page limits are read from the reply via _HandlePrintSetup().
 *
 *  @return B_OK if the user confirmed and the reply contained valid first/last
 *          page fields; B_ERROR otherwise.
 *  @see ConfigPage()
 *  @see FirstPage()
 *  @see LastPage()
 */
status_t
BPrintJob::ConfigJob()
{
	PrintServerMessenger messenger(PSRV_SHOW_PRINT_SETUP, fSetupMessage);
	status_t status = messenger.SendRequest();
	if (status != B_OK)
		return status;

	delete fSetupMessage;
	fSetupMessage = messenger.Result();
	if (!_HandlePrintSetup(fSetupMessage))
		return B_ERROR;

	fError = B_OK;
	return B_OK;
}


/** @brief Open the spool file and prepare for page rendering.
 *
 *  Creates the spool file under B_USER_PRINTERS_DIRECTORY, writes the
 *  print_file_header and the flattened setup BMessage, then writes the
 *  first _page_header_ record. After a successful return, DrawView() and
 *  SpoolPage() may be called in sequence to record pages.
 *
 *  Does nothing if a spool file is already open, if fCurrentPageHeader
 *  is NULL, or if no setup message has been set.
 *
 *  @note The job name is embedded in the spool file path via _GetMangledName()
 *        to avoid collisions between concurrent jobs.
 *  @see CommitJob()
 *  @see CancelJob()
 *  @see DrawView()
 *  @see SpoolPage()
 */
void
BPrintJob::BeginJob()
{
	fError = B_ERROR;

	// can not start a new job until it has been commited or cancelled
	if (fSpoolFile != NULL || fCurrentPageHeader == NULL)
		return;

	// TODO show alert, setup message is required
	if (fSetupMessage == NULL)
		return;

	// create spool file
	BPath path;
	status_t status = find_directory(B_USER_PRINTERS_DIRECTORY, &path);
	if (status != B_OK)
		return;

	char *printer = _GetCurrentPrinterName();
	if (printer == NULL)
		return;
	MemoryDeleter _(printer);

	path.Append(printer);

	char mangledName[B_FILE_NAME_LENGTH];
	_GetMangledName(mangledName, B_FILE_NAME_LENGTH);

	path.Append(mangledName);
	if (path.InitCheck() != B_OK)
		return;

	// TODO: fSpoolFileName should store the name only (not path which can be
	// 1024 bytes long)
	strlcpy(fSpoolFileName, path.Path(), sizeof(fSpoolFileName));
	fSpoolFile = new BFile(fSpoolFileName, B_READ_WRITE | B_CREATE_FILE);

	if (fSpoolFile->InitCheck() != B_OK) {
		CancelJob();
		return;
	}

	// add print_file_header
	// page_count is updated in CommitJob()
	// on BeOS R5 the offset to the first page was always -1
	fSpoolFileHeader.version = 1 << 16;
	fSpoolFileHeader.page_count = 0;
	fSpoolFileHeader.first_page = (off_t)-1;

	if (fSpoolFile->Write(&fSpoolFileHeader, sizeof(print_file_header))
			!= sizeof(print_file_header)) {
		CancelJob();
		return;
	}

	// add printer settings message
	if (!fSetupMessage->HasString(PSRV_FIELD_CURRENT_PRINTER))
		fSetupMessage->AddString(PSRV_FIELD_CURRENT_PRINTER, printer);

	_AddSetupSpec();
	_NewPage();

	// state variables
	fAbort = 0;
	fError = B_OK;
}


/** @brief Finalise the spool file and hand it to the print server for output.
 *
 *  Updates the on-disk print_file_header with the final page count, sets the
 *  required spool file attributes (MIME type, page count, job name, printer
 *  name, status), closes the file, then sends PSRV_PRINT_SPOOLED_JOB to the
 *  print server. Shows an error alert if no pages were recorded.
 *
 *  @note After CommitJob() returns, the BPrintJob object may be reused for a
 *        new job by calling BeginJob() again.
 *  @see BeginJob()
 *  @see CancelJob()
 */
void
BPrintJob::CommitJob()
{
	if (fSpoolFile == NULL)
		return;

	if (fSpoolFileHeader.page_count == 0) {
		ShowError(B_TRANSLATE("No pages to print!"));
		CancelJob();
		return;
	}

	// update spool file
	_EndLastPage();

	// write spool file header
	fSpoolFile->Seek(0, SEEK_SET);
	fSpoolFile->Write(&fSpoolFileHeader, sizeof(print_file_header));

	// set file attributes
	app_info appInfo;
	be_app->GetAppInfo(&appInfo);
	const char* printerName = "";
	fSetupMessage->FindString(PSRV_FIELD_CURRENT_PRINTER, &printerName);

	BNodeInfo info(fSpoolFile);
	info.SetType(PSRV_SPOOL_FILETYPE);

	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_PAGECOUNT, B_INT32_TYPE, 0,
		&fSpoolFileHeader.page_count, sizeof(int32));
	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_DESCRIPTION, B_STRING_TYPE, 0,
		fPrintJobName, strlen(fPrintJobName) + 1);
	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_PRINTER, B_STRING_TYPE, 0,
		printerName, strlen(printerName) + 1);
	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_STATUS, B_STRING_TYPE, 0,
		PSRV_JOB_STATUS_WAITING, strlen(PSRV_JOB_STATUS_WAITING) + 1);
	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_MIMETYPE, B_STRING_TYPE, 0,
		appInfo.signature, strlen(appInfo.signature) + 1);

	delete fSpoolFile;
	fSpoolFile = NULL;
	fError = B_ERROR;

	// notify print server
	BMessenger printServer;
	if (PrintServerMessenger::GetPrintServerMessenger(printServer) != B_OK)
		return;

	BMessage request(PSRV_PRINT_SPOOLED_JOB);
	request.AddString("JobName", fPrintJobName);
	request.AddString("Spool File", fSpoolFileName);

	BMessage reply;
	printServer.SendMessage(&request, &reply);
}


/** @brief Abort the current print job and delete the partial spool file.
 *
 *  Sets fAbort to 1 so that CanContinue() returns false, removes the spool
 *  file from disk, and closes the BFile object. Safe to call even when no
 *  job is in progress.
 *
 *  @see BeginJob()
 *  @see CanContinue()
 */
void
BPrintJob::CancelJob()
{
	if (fSpoolFile == NULL)
		return;

	fAbort = 1;
	BEntry(fSpoolFileName).Remove();
	delete fSpoolFile;
	fSpoolFile = NULL;
}


/** @brief Finalise the current page and start a new one in the spool file.
 *
 *  Increments the page count, updates the previous _page_header_ record on
 *  disk with the file offset of the new page, then calls _NewPage() to write
 *  a fresh _page_header_. Pages with no recorded pictures are silently skipped.
 *
 *  @see BeginJob()
 *  @see DrawView()
 *  @see CommitJob()
 */
void
BPrintJob::SpoolPage()
{
	if (fSpoolFile == NULL)
		return;

	if (fCurrentPageHeader->number_of_pictures == 0)
		return;

	fSpoolFileHeader.page_count++;
	fSpoolFile->Seek(0, SEEK_END);
	if (fCurrentPageHeaderOffset) {
		// update last written page_header
		fCurrentPageHeader->next_page = fSpoolFile->Position();
		fSpoolFile->Seek(fCurrentPageHeaderOffset, SEEK_SET);
		fSpoolFile->Write(fCurrentPageHeader, sizeof(_page_header_));
		fSpoolFile->Seek(fCurrentPageHeader->next_page, SEEK_SET);
	}

	_NewPage();
}


/** @brief Return whether the job is still in a printable state.
 *  @return true if no error has occurred and the job has not been cancelled;
 *          false if fError is not B_OK or fAbort is set.
 *  @see CancelJob()
 */
bool
BPrintJob::CanContinue()
{
	// Check if our local error storage is still B_OK
	return fError == B_OK && !fAbort;
}


/** @brief Record a rectangular area of a view into the current spool page.
 *
 *  Locks the view's looper, captures the view (and its children) into a
 *  BPicture via _RecurseView(), then writes the picture together with its
 *  bounding rect and destination point to the spool file via _AddPicture().
 *
 *  @param view   The view whose contents should be printed.
 *  @param rect   The rectangle within \a view's coordinate space to capture.
 *  @param where  The point on the printed page where the top-left of \a rect
 *                should be placed.
 *  @see SpoolPage()
 *  @see _RecurseView()
 */
void
BPrintJob::DrawView(BView* view, BRect rect, BPoint where)
{
	if (fSpoolFile == NULL)
		return;

	if (view == NULL)
		return;

	if (view->LockLooper()) {
		BPicture picture;
		_RecurseView(view, B_ORIGIN - rect.LeftTop(), &picture, rect);
		_AddPicture(picture, rect, where);
		view->UnlockLooper();
	}
}


/** @brief Return a copy of the current print-job settings message.
 *  @return A newly allocated BMessage containing the current settings, or NULL
 *          if no settings have been established yet. The caller owns the
 *          returned object and must delete it.
 *  @see SetSettings()
 */
BMessage*
BPrintJob::Settings()
{
	if (fSetupMessage == NULL)
		return NULL;

	return new BMessage(*fSetupMessage);
}


/** @brief Replace the current print-job settings with the supplied message.
 *
 *  Parses \a message through _HandlePrintSetup() to update the paper rect,
 *  printable rect, resolution, and page-limit fields, then takes ownership of
 *  the pointer (deleting the old settings message).
 *
 *  @param message The new settings message. Ownership is transferred to the
 *                 BPrintJob; pass NULL to clear the settings.
 *  @see Settings()
 *  @see IsSettingsMessageValid()
 */
void
BPrintJob::SetSettings(BMessage* message)
{
	if (message != NULL)
		_HandlePrintSetup(message);

	delete fSetupMessage;
	fSetupMessage = message;
}


/** @brief Test whether a settings message is valid for the current printer.
 *  @param message The settings BMessage to validate.
 *  @return true if \a message is non-NULL, contains a "printer_name" field,
 *          and that name matches the currently active printer; false otherwise.
 *  @see Settings()
 *  @see SetSettings()
 */
bool
BPrintJob::IsSettingsMessageValid(BMessage* message) const
{
	char* printerName = _GetCurrentPrinterName();
	if (printerName == NULL)
		return false;

	const char* name = NULL;
	// The passed message is valid if it contains the right printer name.
	bool valid = message != NULL
		&& message->FindString("printer_name", &name) == B_OK
		&& strcmp(printerName, name) == 0;

	free(printerName);
	return valid;
}


// Either SetSettings() or ConfigPage() has to be called prior
// to any of the getters otherwise they return undefined values.
/** @brief Return the full paper rectangle for the current page setup.
 *
 *  If no setup message has been established yet, loads default settings from
 *  the print server. The rectangle is in points at the current resolution.
 *
 *  @return A BRect describing the full paper area, or an undefined value if
 *          neither SetSettings() nor ConfigPage() has been called and the
 *          print server is unavailable.
 *  @see PrintableRect()
 *  @see ConfigPage()
 */
BRect
BPrintJob::PaperRect()
{
	if (fDefaultSetupMessage == NULL)
		_LoadDefaultSettings();

	return fPaperSize;
}


/** @brief Return the printable (margins-inset) rectangle for the current page setup.
 *
 *  If no setup message has been established yet, loads default settings from
 *  the print server.
 *
 *  @return A BRect describing the printable area within the paper, or an
 *          undefined value if no setup has been performed.
 *  @see PaperRect()
 *  @see ConfigPage()
 */
BRect
BPrintJob::PrintableRect()
{
	if (fDefaultSetupMessage == NULL)
		_LoadDefaultSettings();

	return fUsableSize;
}


/** @brief Retrieve the horizontal and vertical print resolution in DPI.
 *
 *  If no setup message has been established yet, loads default settings from
 *  the print server. Either output pointer may be NULL if that value is not
 *  needed.
 *
 *  @param xdpi Receives the horizontal resolution in dots per inch.
 *  @param ydpi Receives the vertical resolution in dots per inch.
 *  @see PaperRect()
 *  @see ConfigPage()
 */
void
BPrintJob::GetResolution(int32* xdpi, int32* ydpi)
{
	if (fDefaultSetupMessage == NULL)
		_LoadDefaultSettings();

	if (xdpi != NULL)
		*xdpi = fXResolution;

	if (ydpi != NULL)
		*ydpi = fYResolution;
}


/** @brief Return the first page number the user selected in the print dialog.
 *  @return The first page to print (1-based), as set by ConfigJob() or
 *          SetSettings().
 *  @see LastPage()
 *  @see ConfigJob()
 */
int32
BPrintJob::FirstPage()
{
	return fFirstPage;
}


/** @brief Return the last page number the user selected in the print dialog.
 *  @return The last page to print (1-based), as set by ConfigJob() or
 *          SetSettings().
 *  @see FirstPage()
 *  @see ConfigJob()
 */
int32
BPrintJob::LastPage()
{
	return fLastPage;
}


/** @brief Query the active printer's colour capability.
 *  @param  Unused reserved parameter; pass NULL.
 *  @return B_COLOR_PRINTER if the active printer supports colour, or the
 *          printer-driver-reported type constant. Defaults to B_COLOR_PRINTER
 *          if the print server cannot be contacted.
 */
int32
BPrintJob::PrinterType(void*) const
{
	BMessenger printServer;
	if (PrintServerMessenger::GetPrintServerMessenger(printServer) != B_OK)
		return B_COLOR_PRINTER; // default

	BMessage reply;
	BMessage message(PSRV_GET_ACTIVE_PRINTER);
	printServer.SendMessage(&message, &reply);

	int32 type;
	if (reply.FindInt32("color", &type) != B_OK)
		return B_COLOR_PRINTER; // default

	return type;
}


// #pragma mark - private


/** @brief Recursively capture a view tree into a BPicture for printing.
 *
 *  Appends drawing commands to \a picture for \a view and all of its visible
 *  children that overlap \a rect. The view's background is filled, Draw() is
 *  called with the print rect, and if B_DRAW_ON_CHILDREN is set,
 *  DrawAfterChildren() is also recorded. fIsPrinting is set to true for each
 *  Draw/DrawAfterChildren call so the view can distinguish print from screen
 *  rendering.
 *
 *  @param view    The view to capture.
 *  @param origin  Accumulated origin offset to apply before drawing.
 *  @param picture The BPicture to append drawing commands into.
 *  @param rect    The rectangle (in \a view's coordinates) to capture.
 *  @see DrawView()
 */
void
BPrintJob::_RecurseView(BView* view, BPoint origin, BPicture* picture,
	BRect rect)
{
	ASSERT(picture != NULL);

	BRegion region;
	region.Set(BRect(rect.left, rect.top, rect.right, rect.bottom));
	view->fState->print_rect = rect;

	view->AppendToPicture(picture);
	view->PushState();
	view->SetOrigin(origin);
	view->ConstrainClippingRegion(&region);

	if (view->ViewColor() != B_TRANSPARENT_COLOR) {
		rgb_color highColor = view->HighColor();
		view->SetHighColor(view->ViewColor());
		view->FillRect(rect);
		view->SetHighColor(highColor);
	}

	if ((view->Flags() & B_WILL_DRAW) != 0) {
		view->fIsPrinting = true;
		view->Draw(rect);
		view->fIsPrinting = false;
	}

	view->PopState();
	view->EndPicture();

	BView* child = view->ChildAt(0);
	while (child != NULL) {
		if (!child->IsHidden()) {
			BPoint leftTop(view->Bounds().LeftTop() + child->Frame().LeftTop());
			BRect printRect(rect.OffsetToCopy(rect.LeftTop() - leftTop)
				& child->Bounds());
			if (printRect.IsValid())
				_RecurseView(child, origin + leftTop, picture, printRect);
		}
		child = child->NextSibling();
	}

	if ((view->Flags() & B_DRAW_ON_CHILDREN) != 0) {
		view->AppendToPicture(picture);
		view->PushState();
		view->SetOrigin(origin);
		view->ConstrainClippingRegion(&region);
		view->fIsPrinting = true;
		view->DrawAfterChildren(rect);
		view->fIsPrinting = false;
		view->PopState();
		view->EndPicture();
	}
}


/** @brief Build a unique spool file name from the job name and current time.
 *  @param buffer     Output buffer to receive the mangled name string.
 *  @param bufferSize Size of \a buffer in bytes.
 *  @note The timestamp is in milliseconds from system_time(), ensuring
 *        uniqueness even for jobs with the same name.
 */
void
BPrintJob::_GetMangledName(char* buffer, size_t bufferSize) const
{
	snprintf(buffer, bufferSize, "%s@%" B_PRId64, fPrintJobName,
		system_time() / 1000);
}


/** @brief Extract paper and resolution fields from a page-setup BMessage.
 *
 *  Reads PSRV_FIELD_PAPER_RECT, PSRV_FIELD_PRINTABLE_RECT, PSRV_FIELD_XRES,
 *  and PSRV_FIELD_YRES from \a setup and stores them in fPaperSize, fUsableSize,
 *  fXResolution, and fYResolution respectively.
 *
 *  @param setup The page-setup message returned by the print server.
 *  @see _HandlePrintSetup()
 *  @see ConfigPage()
 */
void
BPrintJob::_HandlePageSetup(BMessage* setup)
{
	setup->FindRect(PSRV_FIELD_PRINTABLE_RECT, &fUsableSize);
	setup->FindRect(PSRV_FIELD_PAPER_RECT, &fPaperSize);

	// TODO verify data type (taken from libprint)
	int64 valueInt64;
	if (setup->FindInt64(PSRV_FIELD_XRES, &valueInt64) == B_OK)
		fXResolution = (short)valueInt64;

	if (setup->FindInt64(PSRV_FIELD_YRES, &valueInt64) == B_OK)
		fYResolution = (short)valueInt64;
}


/** @brief Extract all print-job settings (page setup plus page range) from a BMessage.
 *
 *  Calls _HandlePageSetup() for the paper/resolution fields, then reads
 *  PSRV_FIELD_FIRST_PAGE and PSRV_FIELD_LAST_PAGE into fFirstPage and fLastPage.
 *
 *  @param message The job-setup message returned by the print server.
 *  @return true if both first-page and last-page fields were found; false if
 *          either field is missing.
 *  @see _HandlePageSetup()
 *  @see ConfigJob()
 */
bool
BPrintJob::_HandlePrintSetup(BMessage* message)
{
	_HandlePageSetup(message);

	bool valid = true;
	if (message->FindInt32(PSRV_FIELD_FIRST_PAGE, &fFirstPage) != B_OK)
		valid = false;

	if (message->FindInt32(PSRV_FIELD_LAST_PAGE, &fLastPage) != B_OK)
		valid = false;

	return valid;
}


/** @brief Write a fresh _page_header_ placeholder at the current spool file position.
 *
 *  Resets the per-page picture count and next-page offset to zero, records
 *  the file offset of this header (so SpoolPage() can update it later), and
 *  writes the structure to disk.
 *
 *  @see SpoolPage()
 *  @see _EndLastPage()
 */
void
BPrintJob::_NewPage()
{
	// init, write new page_header
	fCurrentPageHeader->next_page = 0;
	fCurrentPageHeader->number_of_pictures = 0;
	fCurrentPageHeaderOffset = fSpoolFile->Position();
	fSpoolFile->Write(fCurrentPageHeader, sizeof(_page_header_));
}


/** @brief Finalise the last page's _page_header_ record on disk.
 *
 *  If the last page has at least one picture, increments the total page count,
 *  seeks back to the saved header offset, writes the updated header (with
 *  next_page == 0 to mark it as the final page), then seeks back to the end
 *  of the file.
 *
 *  @see CommitJob()
 *  @see _NewPage()
 */
void
BPrintJob::_EndLastPage()
{
	if (!fSpoolFile)
		return;

	if (fCurrentPageHeader->number_of_pictures == 0)
		return;

	fSpoolFileHeader.page_count++;
	fSpoolFile->Seek(0, SEEK_END);
	if (fCurrentPageHeaderOffset) {
		fCurrentPageHeader->next_page = 0;
		fSpoolFile->Seek(fCurrentPageHeaderOffset, SEEK_SET);
		fSpoolFile->Write(fCurrentPageHeader, sizeof(_page_header_));
		fSpoolFile->Seek(0, SEEK_END);
	}
}


/** @brief Flatten the current setup BMessage into the spool file.
 *
 *  Called once during BeginJob() immediately after the print_file_header has
 *  been written. The printer driver reads this block when processing the spool
 *  file to recover job settings.
 *
 *  @see BeginJob()
 */
void
BPrintJob::_AddSetupSpec()
{
	fSetupMessage->Flatten(fSpoolFile);
}


/** @brief Append a captured view picture to the current page in the spool file.
 *
 *  Increments the picture count in fCurrentPageHeader, writes the destination
 *  point (\a where) and bounding rectangle (\a rect) to the spool file, then
 *  flattens \a picture.
 *
 *  @param picture The BPicture containing the recorded drawing commands.
 *  @param rect    The source bounding rectangle of the captured view area.
 *  @param where   The destination point on the printed page.
 *  @see DrawView()
 */
void
BPrintJob::_AddPicture(BPicture& picture, BRect& rect, BPoint& where)
{
	ASSERT(fSpoolFile != NULL);

	fCurrentPageHeader->number_of_pictures++;
	fSpoolFile->Write(&where, sizeof(BRect));
	fSpoolFile->Write(&rect, sizeof(BPoint));
	picture.Flatten(fSpoolFile);
}


/*!	Returns a copy of the applications default printer name or NULL if it
	could not be obtained. Caller is responsible to free the string using
	free().
*/
/** @brief Query the print server for the name of the currently active printer.
 *  @return A newly allocated string containing the printer name, or NULL if
 *          the print server is unavailable or returned no name. The caller
 *          must free the returned string with free().
 */
char*
BPrintJob::_GetCurrentPrinterName() const
{
	BMessenger printServer;
	if (PrintServerMessenger::GetPrintServerMessenger(printServer) != B_OK)
		return NULL;

	const char* printerName = NULL;

	BMessage reply;
	BMessage message(PSRV_GET_ACTIVE_PRINTER);
	if (printServer.SendMessage(&message, &reply) == B_OK)
		reply.FindString("printer_name", &printerName);

	if (printerName == NULL)
		return NULL;

	return strdup(printerName);
}


/** @brief Fetch default print settings from the print server and cache them.
 *
 *  Sends PSRV_GET_DEFAULT_SETTINGS to the print server. The reply is stored
 *  in fDefaultSetupMessage. If no fSetupMessage has been established yet, the
 *  reply is also parsed by _HandlePrintSetup() to populate the paper size,
 *  resolution, and page-range fields.
 *
 *  @see PaperRect()
 *  @see PrintableRect()
 *  @see GetResolution()
 */
void
BPrintJob::_LoadDefaultSettings()
{
	BMessenger printServer;
	if (PrintServerMessenger::GetPrintServerMessenger(printServer) != B_OK)
		return;

	BMessage message(PSRV_GET_DEFAULT_SETTINGS);
	BMessage* reply = new BMessage;

	printServer.SendMessage(&message, reply);

	// Only override our settings if we don't have any settings yet
	if (fSetupMessage == NULL)
		_HandlePrintSetup(reply);

	delete fDefaultSetupMessage;
	fDefaultSetupMessage = reply;
}


void BPrintJob::_ReservedPrintJob1() {}
void BPrintJob::_ReservedPrintJob2() {}
void BPrintJob::_ReservedPrintJob3() {}
void BPrintJob::_ReservedPrintJob4() {}


// #pragma mark -- PrintServerMessenger


namespace BPrivate {


/** @brief Construct a PrintServerMessenger and immediately block user input.
 *  @param what  The message code to send to the print server.
 *  @param input Optional BMessage whose contents are merged into the request;
 *               may be NULL.
 *
 *  Creates a hidden application-modal BAlert to prevent the user from
 *  interacting with application windows while the print-server dialog is open.
 *
 *  @see SendRequest()
 */
PrintServerMessenger::PrintServerMessenger(uint32 what, BMessage *input)
	:
	fWhat(what),
	fInput(input),
	fRequest(NULL),
	fResult(NULL),
	fThreadCompleted(-1),
	fHiddenApplicationModalWindow(NULL)
{
	RejectUserInput();
}


/** @brief Destroy the PrintServerMessenger and restore user input.
 *
 *  Deletes the completion semaphore (if SendRequest() was never called or
 *  failed before spawning the thread), frees the request message, and
 *  dismisses the hidden modal window so user input is re-enabled.
 *
 *  @see AllowUserInput()
 */
PrintServerMessenger::~PrintServerMessenger()
{
	DeleteSemaphore();
		// in case SendRequest could not start the thread
	delete fRequest; fRequest = NULL;
	AllowUserInput();
}


/** @brief Create a hidden modal window to block application user input.
 *
 *  Opens a BAlert off-screen (at position -65000, -65000) with its default
 *  button disabled, making the application modal so no other windows can be
 *  interacted with while the print-server request is in progress.
 *
 *  @see AllowUserInput()
 */
void
PrintServerMessenger::RejectUserInput()
{
	fHiddenApplicationModalWindow = new BAlert("bogus", "app_modal", "OK");
	fHiddenApplicationModalWindow->DefaultButton()->SetEnabled(false);
	fHiddenApplicationModalWindow->SetDefaultButton(NULL);
	fHiddenApplicationModalWindow->SetFlags(fHiddenApplicationModalWindow->Flags() | B_CLOSE_ON_ESCAPE);
	fHiddenApplicationModalWindow->MoveTo(-65000, -65000);
	fHiddenApplicationModalWindow->Go(NULL);
}


/** @brief Dismiss the hidden modal window and re-enable application user input.
 *
 *  Locks and quits the hidden BAlert created by RejectUserInput(), allowing
 *  normal application event processing to resume.
 *
 *  @see RejectUserInput()
 */
void
PrintServerMessenger::AllowUserInput()
{
	fHiddenApplicationModalWindow->Lock();
	fHiddenApplicationModalWindow->Quit();
}


/** @brief Delete the completion semaphore if it has not already been removed.
 *
 *  Guards against double-deletion by checking fThreadCompleted >= B_OK before
 *  calling delete_sem(). Sets fThreadCompleted to -1 after deletion.
 *
 *  @see SendRequest()
 *  @see SetResult()
 */
void
PrintServerMessenger::DeleteSemaphore()
{
	if (fThreadCompleted >= B_OK) {
		sem_id id = fThreadCompleted;
		fThreadCompleted = -1;
		delete_sem(id);
	}
}


/** @brief Send the request to the print server and wait for the reply.
 *
 *  Creates a semaphore, spawns MessengerThread() at normal priority to perform
 *  the actual send, then blocks while pumping the originating window's event
 *  loop (if any) with 50 ms time-slice acquire attempts. Once the thread
 *  signals completion via SetResult(), waits for the thread to exit before
 *  returning.
 *
 *  @return B_OK if the print server replied successfully (Result() is non-NULL);
 *          B_ERROR if the semaphore could not be created, the thread could not
 *          be spawned, or the print server returned an error.
 *  @see MessengerThread()
 *  @see SetResult()
 */
status_t
PrintServerMessenger::SendRequest()
{
	fThreadCompleted = create_sem(0, "print_server_messenger_sem");
	if (fThreadCompleted < B_OK)
		return B_ERROR;

	thread_id id = spawn_thread(MessengerThread, "async_request",
		B_NORMAL_PRIORITY, this);
	if (id <= 0 || resume_thread(id) != B_OK)
		return B_ERROR;

	// Get the originating window, if it exists
	BWindow* window = dynamic_cast<BWindow*>(
		BLooper::LooperForThread(find_thread(NULL)));
	if (window != NULL) {
		status_t err;
		while (true) {
			do {
				err = acquire_sem_etc(fThreadCompleted, 1, B_RELATIVE_TIMEOUT,
					50000);
			// We've (probably) had our time slice taken away from us
			} while (err == B_INTERRUPTED);

			// Semaphore was finally nuked in SetResult(BMessage *)
			if (err == B_BAD_SEM_ID)
				break;
			window->UpdateIfNeeded();
		}
	} else {
		// No window to update, so just hang out until we're done.
		while (acquire_sem(fThreadCompleted) == B_INTERRUPTED);
	}

	status_t status;
	wait_for_thread(id, &status);

	return Result() != NULL ? B_OK : B_ERROR;
}


/** @brief Build (or return the cached) request BMessage to send to the print server.
 *
 *  On the first call, creates a new BMessage: if fInput is non-NULL it copies
 *  fInput and overwrites its what field with fWhat; otherwise it creates an
 *  empty message with fWhat. Subsequent calls return the same object.
 *
 *  @return The request BMessage to be sent, or NULL on allocation failure.
 *  @see SendRequest()
 */
BMessage*
PrintServerMessenger::Request()
{
	if (fRequest != NULL)
		return fRequest;

	if (fInput != NULL) {
		fRequest = new BMessage(*fInput);
		fRequest->what = fWhat;
	} else
		fRequest = new BMessage(fWhat);

	return fRequest;
}


/** @brief Store the print-server reply and signal the waiting SendRequest() call.
 *
 *  Called from MessengerThread() after receiving the server's reply. Saves
 *  \a result in fResult, then deletes the completion semaphore, which causes
 *  SendRequest()'s acquire_sem_etc() loop to break on B_BAD_SEM_ID.
 *
 *  @param result The reply BMessage from the print server, or NULL on failure.
 *  @see SendRequest()
 *  @see DeleteSemaphore()
 */
void
PrintServerMessenger::SetResult(BMessage* result)
{
	fResult = result;
	DeleteSemaphore();
	// terminate loop in thread spawned by SendRequest
}


/** @brief Obtain a BMessenger targeting the running print server.
 *  @param messenger Receives the messenger on success.
 *  @return B_OK if the print server is running and the messenger is valid;
 *          B_ERROR if the print server cannot be found.
 */
status_t
PrintServerMessenger::GetPrintServerMessenger(BMessenger& messenger)
{
	messenger = BMessenger(PSRV_SIGNATURE_TYPE);
	return messenger.IsValid() ? B_OK : B_ERROR;
}


/** @brief Thread entry point that sends the request and stores the reply.
 *
 *  Runs in a dedicated thread spawned by SendRequest(). Contacts the print
 *  server, sends the request message built by Request(), and stores the
 *  reply via SetResult(). Shows an error alert if the print server is
 *  unreachable. Always calls SetResult() (with NULL on failure) so that
 *  SendRequest() is always unblocked.
 *
 *  @param data Pointer to the owning PrintServerMessenger instance.
 *  @return B_OK on success; B_ERROR if the print server is unreachable, the
 *          request could not be built, or the reply was not 'okok'.
 *  @see SendRequest()
 *  @see SetResult()
 */
status_t
PrintServerMessenger::MessengerThread(void* data)
{
	PrintServerMessenger* messenger = static_cast<PrintServerMessenger*>(data);

	BMessenger printServer;
	if (messenger->GetPrintServerMessenger(printServer) != B_OK) {
		ShowError(B_TRANSLATE("Print Server is not responding."));
		messenger->SetResult(NULL);
		return B_ERROR;
	}

	BMessage* request = messenger->Request();
	if (request == NULL) {
		messenger->SetResult(NULL);
		return B_ERROR;
	}


	BMessage reply;
	if (printServer.SendMessage(request, &reply) != B_OK
		|| reply.what != 'okok' ) {
		messenger->SetResult(NULL);
		return B_ERROR;
	}

	messenger->SetResult(new BMessage(reply));
	return B_OK;
}


}	// namespace BPrivate
