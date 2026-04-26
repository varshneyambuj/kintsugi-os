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
 *   Copyright 2001-2011, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 *       Philippe Houdoin
 */


/**
 * @file PrintersWindow.cpp
 * @brief Main window of the Printers preference panel.
 *
 * Hosts two boxes: the printer list (with Add / Remove / Make default /
 * Print test page actions) and the job list (with Cancel / Restart). Also
 * defines the small TestPageWindow used to render a test page off-screen
 * before spooling it.
 *
 * @see PrinterListView, JobListView, AddPrinterDialog
 */


#include "PrintersWindow.h"

#include <stdio.h>

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <Layout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <PrintJob.h>
#include <ScrollView.h>

#include "pr_server.h"
#include "AddPrinterDialog.h"
#include "Globals.h"
#include "JobListView.h"
#include "Messages.h"
#include "PrinterListView.h"
#include "TestPageView.h"
#include "ScreenSettings.h"
#include "SpoolFolder.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrintersWindow"


/**
 * @brief Off-screen window that renders and spools a single test page.
 *
 * The window is never shown to the user; it lives only long enough for
 * the BPrintJob to drive the TestPageView through draw / spool / commit
 * before quitting itself.
 */
class TestPageWindow : public BWindow {
public:
						TestPageWindow(BPrintJob* job, PrinterItem* printer);
	virtual				~TestPageWindow();

			void		MessageReceived(BMessage* message);
private:
			BPrintJob*	fJob;
			TestPageView*	fTestPage;
};


/**
 * @brief Constructs the off-screen TestPageWindow.
 *
 * Positions the window far off-screen (-20000, -20000) so the BWindow
 * machinery has somewhere to attach the view tree without flashing it on
 * the user's display.
 *
 * @param job     BPrintJob configured by the caller; the window takes
 *                ownership.
 * @param printer Target printer used by TestPageView for status text.
 */
TestPageWindow::TestPageWindow(BPrintJob* job, PrinterItem* printer)
	: BWindow(job->PaperRect().OffsetByCopy(-20000, -20000),
		B_TRANSLATE("Test page"),
		B_TITLED_WINDOW, 0), fJob(job)
{
	fTestPage = new TestPageView(job->PrintableRect(), printer);

	// SetLayout(new BGroupLayout(B_VERTICAL));
	AddChild(fTestPage);
}


/** @brief Destructor; releases the held BPrintJob. */
TestPageWindow::~TestPageWindow()
{
	delete fJob;
}


/**
 * @brief Drives the BPrintJob through one page when kMsgPrintTestPage
 *        arrives.
 *
 * Calls BeginJob -> DrawView -> SpoolPage -> CommitJob then quits the
 * window so its destructor frees the print job.
 *
 * @param message Incoming BMessage; only kMsgPrintTestPage is handled.
 */
void
TestPageWindow::MessageReceived(BMessage* message)
{
	if (message->what != kMsgPrintTestPage) {
		BWindow::MessageReceived(message);
		return;
	}

	fJob->BeginJob();

	fJob->DrawView(fTestPage, fTestPage->Bounds(), B_ORIGIN);
	fJob->SpoolPage();

	if (!fJob->CanContinue())
		return;

	fJob->CommitJob();

	Quit();
}


// #pragma mark PrintersWindow main class


/**
 * @brief Constructs the main window using a persisted frame.
 *
 * Builds the GUI and ensures the saved position falls on a visible
 * screen. Takes ownership of @a settings.
 *
 * @param settings Persisted screen settings; freed in the destructor.
 */
PrintersWindow::PrintersWindow(ScreenSettings* settings)
	:
	BWindow(settings->WindowFrame(), B_TRANSLATE_SYSTEM_NAME("Printers"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fSettings(settings),
	fSelectedPrinter(NULL),
	fAddingPrinter(false)
{
	_BuildGUI();
	MoveOnScreen();
}


/**
 * @brief Destructor; persists the screen settings and frees them.
 */
PrintersWindow::~PrintersWindow()
{
	delete fSettings;
}


/**
 * @brief Saves window geometry then quits the application.
 *
 * Writes the current Frame() into ScreenSettings before deferring to the
 * BWindow base class. If the base allows the close, also tells the
 * BApplication to quit.
 *
 * @return Whatever BWindow::QuitRequested() returns.
 */
bool
PrintersWindow::QuitRequested()
{
	fSettings->SetWindowFrame(Frame());

	bool result = Inherited::QuitRequested();
	if (result)
		be_app->PostMessage(B_QUIT_REQUESTED);

	return result;
}


/**
 * @brief Dispatches messages from the printer and job lists and toolbar
 *        buttons.
 *
 * Reacts to selection changes, add/remove/make-default actions, the test
 * page button, cancel/restart job buttons, and B_PRINTER_CHANGED
 * broadcasts forwarded by PrintersApp.
 *
 * @param msg Incoming BMessage.
 */
void
PrintersWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgPrinterSelected:
		{
			fSelectedPrinter = fPrinterListView->SelectedItem();
			if (fSelectedPrinter) {
				BString text = B_TRANSLATE("Print jobs for %printer_name%");
				text.ReplaceFirst("%printer_name%", fSelectedPrinter->Name());

				fJobsBox->SetLabel(text);
				fMakeDefault->SetEnabled(true);
				fRemove->SetEnabled(true);
				fJobListView->SetSpoolFolder(fSelectedPrinter->Folder());
			} else {
				fJobsBox->SetLabel(
					B_TRANSLATE("Print jobs: No printer selected"));
				fMakeDefault->SetEnabled(false);
				fRemove->SetEnabled(false);
				fSelectedPrinter = NULL;
				fJobListView->SetSpoolFolder(NULL);
			}
			_UpdateJobButtons();
			_UpdatePrinterButtons();
			break;
		}

		case kMsgAddPrinter:
			if (!fAddingPrinter) {
				fAddingPrinter = true;
				new AddPrinterDialog(this);
			}
			break;

		case kMsgAddPrinterClosed:
			fAddingPrinter = false;
			break;

		case kMsgRemovePrinter:
		{
			fSelectedPrinter = fPrinterListView->SelectedItem();
			if (fSelectedPrinter)
				fSelectedPrinter->Remove(fPrinterListView);
			break;
		}

		case kMsgMakeDefaultPrinter:
		{
			PrinterItem* printer = fPrinterListView->SelectedItem();
			if (printer && printer == fPrinterListView->ActivePrinter())
				break;
			BMessenger msgr;
			if (printer && GetPrinterServerMessenger(msgr) == B_OK) {
				BMessage setActivePrinter(B_SET_PROPERTY);
				setActivePrinter.AddSpecifier("ActivePrinter");
				setActivePrinter.AddString("data", printer->Name());
				msgr.SendMessage(&setActivePrinter);
				_UpdatePrinterButtons();
			}
			break;
		}

		case kMsgPrintTestPage:
		{
			fSelectedPrinter = fPrinterListView->SelectedItem();
			if (fSelectedPrinter)
				PrintTestPage(fSelectedPrinter);
			break;
		}

		case kMsgCancelJob:
			fJobListView->CancelJob();
			break;

		case kMsgRestartJob:
			fJobListView->RestartJob();
			break;

		case kMsgJobSelected:
			_UpdateJobButtons();
			break;

		case B_PRINTER_CHANGED:
		{
			// active printer could have been changed, even outside of prefs
			BString activePrinterName(ActivePrinterName());
			PrinterItem* item = fPrinterListView->ActivePrinter();
			if (item && item->Name() != activePrinterName)
				fPrinterListView->UpdateItem(item);

			for (int32 i = 0; i < fPrinterListView->CountItems(); ++i) {
				item = dynamic_cast<PrinterItem*>(fPrinterListView->ItemAt(i));
				if (item && item->Name() == activePrinterName) {
					fPrinterListView->UpdateItem(item);
					fPrinterListView->SetActivePrinter(item);
					break;
				}
			}
		}	break;

		default:
			Inherited::MessageReceived(msg);
	}
}


/**
 * @brief Configures and submits a test page on @a printer.
 *
 * Calls BPrintJob::ConfigPage() so the user can pick paper / orientation
 * settings, then forces single-copy / all-pages settings and hands the
 * job to a TestPageWindow which drives it through to the spool folder.
 *
 * @param printer PrinterItem identifying the destination printer.
 */
void
PrintersWindow::PrintTestPage(PrinterItem* printer)
{
	BPrintJob* job = new BPrintJob(B_TRANSLATE("Test page"));
	job->ConfigPage();

	// job->ConfigJob();

	BMessage* settings = job->Settings();
	if (settings == NULL) {
		delete job;
		return;
	}

	// enforce job config properties
	settings->AddInt32("copies", 1);
	settings->AddInt32("first_page", 1);
	settings->AddInt32("last_page", -1);

	BWindow* win = new TestPageWindow(job, printer);
	win->Show();
	win->PostMessage(kMsgPrintTestPage);
}


/**
 * @brief SpoolFolder callback invoked when a new job is queued.
 *
 * Adds the job to the visible JobListView when its printer is currently
 * selected, refreshes the printer-row pending-jobs string, and updates
 * the toolbar buttons.
 *
 * @param folder SpoolFolder reporting the change.
 * @param job    Newly queued job.
 */
void
PrintersWindow::AddJob(SpoolFolder* folder, Job* job)
{
	if (_IsSelected(folder->Item()))
		fJobListView->AddJob(job);
	fPrinterListView->UpdateItem(folder->Item());
	_UpdatePrinterButtons();
}


/**
 * @brief SpoolFolder callback invoked when a job is removed from the
 *        spool.
 *
 * @param folder SpoolFolder reporting the change.
 * @param job    Job that disappeared.
 */
void
PrintersWindow::RemoveJob(SpoolFolder* folder, Job* job)
{
	if (_IsSelected(folder->Item()))
		fJobListView->RemoveJob(job);
	fPrinterListView->UpdateItem(folder->Item());
	_UpdatePrinterButtons();
}


/**
 * @brief SpoolFolder callback invoked when a job's attributes change.
 *
 * @param folder SpoolFolder reporting the change.
 * @param job    Job whose attributes have changed.
 */
void
PrintersWindow::UpdateJob(SpoolFolder* folder, Job* job)
{
	if (_IsSelected(folder->Item())) {
		fJobListView->UpdateJob(job);
		_UpdateJobButtons();
	}
	fPrinterListView->UpdateItem(folder->Item());
	_UpdatePrinterButtons();
}


// #pragma mark -


/**
 * @brief Builds the entire window layout.
 *
 * Creates the printers and jobs boxes, wires up buttons, and finally
 * normalises the widths of the right-column action buttons so the two
 * boxes align visually.
 */
void
PrintersWindow::_BuildGUI()
{
// ------------------------ Next, build the printers overview box
	BBox* printersBox = new BBox("printersBox");
	printersBox->SetFont(be_bold_font);
	printersBox->SetLabel(B_TRANSLATE("Printers"));

		// Add Button
	BButton* addButton = new BButton("add",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddPrinter));
	addButton->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		// Remove button
	fRemove = new BButton("remove",
		B_TRANSLATE("Remove"), new BMessage(kMsgRemovePrinter));
	fRemove->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		// Make Default button
	fMakeDefault = new BButton("default",
		B_TRANSLATE("Make default"), new BMessage(kMsgMakeDefaultPrinter));
	fMakeDefault->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		// Print Test Page button
	fPrintTestPage = new BButton("print_test_page",
		B_TRANSLATE("Print test page"), new BMessage(kMsgPrintTestPage));
	fPrintTestPage->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		// Disable all selection-based buttons
	fRemove->SetEnabled(false);
	fMakeDefault->SetEnabled(false);
	fPrintTestPage->SetEnabled(false);

		// Create listview with scroller
	fPrinterListView = new PrinterListView(BRect());
	BScrollView* printerScrollView = new BScrollView("printer_scroller",
		fPrinterListView, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS,
		false, true, B_FANCY_BORDER);

	printerScrollView->SetExplicitMinSize(
		BSize(be_plain_font->Size() * 30, B_SIZE_UNSET));

	float padding = be_control_look->DefaultItemSpacing();

	BLayoutBuilder::Group<>(printersBox, B_HORIZONTAL, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(printerScrollView)
		.AddGroup(B_VERTICAL, padding / 2, 0.0f)
			.SetInsets(0)
			.Add(addButton)
			.Add(fRemove)
			.Add(fMakeDefault)
			.Add(fPrintTestPage)
			.AddGlue();

// ------------------------ Lastly, build the jobs overview box
	fJobsBox = new BBox("jobsBox");
	fJobsBox->SetFont(be_bold_font);
	fJobsBox->SetLabel(B_TRANSLATE("Print jobs: No printer selected"));

		// Cancel Job Button
	fCancel = new BButton("cancel",
		B_TRANSLATE("Cancel job"), new BMessage(kMsgCancelJob));
	fCancel->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		// Restart Job button
	fRestart = new BButton("restart",
		B_TRANSLATE("Restart job"), new BMessage(kMsgRestartJob));
	fRestart->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		// Disable all selection-based buttons
	fCancel->SetEnabled(false);
	fRestart->SetEnabled(false);

		// Create listview with scroller
	fJobListView = new JobListView(BRect());
	BScrollView* jobScrollView = new BScrollView("jobs_scroller",
		fJobListView, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS,
		false, true, B_FANCY_BORDER);

	BLayoutBuilder::Group<>(fJobsBox, B_HORIZONTAL, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(jobScrollView)
		.AddGroup(B_VERTICAL, padding / 2, 0.0f)
			.SetInsets(0)
			.Add(fCancel)
			.Add(fRestart)
			.AddGlue();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(printersBox)
		.AddStrut(B_USE_DEFAULT_SPACING)
		.Add(fJobsBox);

		// There is a better solution?
	Layout(true);
	if (fPrintTestPage->Bounds().Width() > fRestart->Bounds().Width())
		fRestart->SetExplicitMinSize(
			BSize(fPrintTestPage->Bounds().Width(), B_SIZE_UNSET));
	else
		fPrintTestPage->SetExplicitMinSize(
			BSize(fRestart->Bounds().Width(), B_SIZE_UNSET));
}


/**
 * @brief Returns true when @a printer matches the currently selected row.
 *
 * @param printer Candidate PrinterItem.
 *
 * @return True when the row is currently selected.
 */
bool
PrintersWindow::_IsSelected(PrinterItem* printer)
{
	return fSelectedPrinter && fSelectedPrinter == printer;
}


/**
 * @brief Refreshes the enabled state of printer-related toolbar buttons.
 *
 * Remove is enabled only when a printer is selected and has no pending
 * jobs; Make default is offered for non-active printers; Print test page
 * is offered whenever a printer is selected.
 */
void
PrintersWindow::_UpdatePrinterButtons()
{
	PrinterItem* item = fPrinterListView->SelectedItem();
	fRemove->SetEnabled(item && !item->HasPendingJobs());
	fMakeDefault->SetEnabled(item && !item->IsActivePrinter());
	fPrintTestPage->SetEnabled(item);
}


/**
 * @brief Refreshes the enabled state of the Cancel / Restart buttons
 *        based on the currently selected job.
 */
void
PrintersWindow::_UpdateJobButtons()
{
	JobItem* item = fJobListView->SelectedItem();
	if (item != NULL) {
		Job* job = item->GetJob();
		fCancel->SetEnabled(job->Status() != kProcessing);
		fRestart->SetEnabled(job->Status() == kFailed);
	} else {
		fCancel->SetEnabled(false);
		fRestart->SetEnabled(false);
	}
}


