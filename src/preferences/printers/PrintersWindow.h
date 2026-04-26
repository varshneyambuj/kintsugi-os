/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2001-2010, Haiku.
 * Original authors: Michael Pfeiffer.
 */

/** @file PrintersWindow.h
    @brief Main window of the Printers preference panel. */

#ifndef _PRINTERS_WINDOW_H
#define _PRINTERS_WINDOW_H


#include <Box.h>
#include <Window.h>


class PrintersWindow;
class PrinterListView;
class JobListView;
class Job;
class SpoolFolder;
class PrinterItem;
class ScreenSettings;


/**
 * @brief Main window of the Printers preference panel.
 *
 * Coordinates the printer list view and the job list view, owns the
 * persisted ScreenSettings, and exposes hooks invoked by SpoolFolder when
 * the spool directory changes.
 */
class PrintersWindow : public BWindow {
public:
				PrintersWindow(ScreenSettings *settings);
	virtual		~PrintersWindow();

	void		MessageReceived(BMessage* msg);
	bool		QuitRequested();

	void		PrintTestPage(PrinterItem* printer);

	void		AddJob(SpoolFolder* folder, Job* job);
	void		RemoveJob(SpoolFolder* folder, Job* job);
	void		UpdateJob(SpoolFolder* folder, Job* job);

private:
	ScreenSettings*	fSettings;
	void		_BuildGUI();
	bool		_IsSelected(PrinterItem* printer);
	void		_UpdatePrinterButtons();
	void		_UpdateJobButtons();

	typedef BWindow Inherited;

	PrinterListView*	fPrinterListView;
	BButton*	fMakeDefault;
	BButton*	fRemove;
	BButton*	fPrintTestPage;

	JobListView*	fJobListView;
	BButton*	fRestart;
	BButton*    fCancel;

	BBox*		fJobsBox;

	PrinterItem*	fSelectedPrinter;

	bool		fAddingPrinter;
};

#endif	// _PRINTERS_WINDOW_H
