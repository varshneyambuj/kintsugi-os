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
 *   Copyright 2002-2008, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */

/** @file ConfigWindow.h
 *  @brief Print configuration dialog window for page setup and job setup. */

#ifndef _CONFIG_WINDOW_H
#define _CONFIG_WINDOW_H


#include <InterfaceKit.h>
#include <Window.h>

#include "BeUtils.h"
#include "ObjectList.h"
#include "Printer.h"


enum config_setup_kind {
	kPageSetup,
	kJobSetup,
};


/** @brief Dialog window for configuring page setup and print job settings. */
class ConfigWindow : public BWindow {
	enum {
		MSG_PAGE_SETUP       = 'cwps',
		MSG_JOB_SETUP        = 'cwjs',
		MSG_PRINTER_SELECTED = 'cwpr',
		MSG_OK               = 'cwok',
		MSG_CANCEL           = 'cwcl',
	};

public:
	/** @brief Construct a config window for page or job setup. */
					ConfigWindow(config_setup_kind kind,
						Printer* defaultPrinter, BMessage* settings,
						AutoReply* sender);
	/** @brief Destructor releasing current printer and semaphore. */
					~ConfigWindow();
	/** @brief Display the window modally and block until dismissed. */
			void	 Go();

	/** @brief Handle UI messages such as printer selection, setup, and OK/Cancel. */
			void	MessageReceived(BMessage* m);
	/** @brief Display an about dialog for the print server. */
			void	AboutRequested();
	/** @brief Track window position changes for persistence. */
			void	FrameMoved(BPoint p);

	/** @brief Retrieve the saved window frame rectangle. */
	static	BRect	GetWindowFrame();
	/** @brief Store the window frame rectangle for future sessions. */
	static	void	SetWindowFrame(BRect frame);

private:
			BButton* AddPictureButton(BView* panel, const char* name,
								const char* picture, uint32 what);
			void	PrinterForMimeType();
			void	SetupPrintersMenu(BMenu* menu);
			void	UpdateAppSettings(const char* mime, const char* printer);
			void	UpdateSettings(bool read);
			void	UpdateUI();
			void	Setup(config_setup_kind);

			config_setup_kind fKind; /**< Page setup or job setup mode */
			Printer*		fDefaultPrinter; /**< The system default printer */
			BMessage*		fSettings; /**< Incoming settings message */
			AutoReply*		fSender; /**< Auto-reply helper for async response */
			BString			fSenderMimeType; /**< MIME type of the requesting application */

			BString			fPrinterName; /**< Name of the currently selected printer */
			Printer*		fCurrentPrinter; /**< Currently selected printer object */
			BMessage		fPageSettings; /**< Current page setup settings */
			BMessage		fJobSettings; /**< Current job setup settings */

			sem_id			fFinished; /**< Semaphore signaling window dismissal */

			BMenuField*		fPrinters; /**< Printer selection menu field */
			BButton*		fPageSetup; /**< Page setup button */
			BButton*		fJobSetup; /**< Job setup button */
			BButton*		fOk; /**< OK confirmation button */
			BStringView*	fPageFormatText; /**< Label showing current page format */
			BStringView*	fJobSetupText; /**< Label showing current job setup info */
};


#endif
