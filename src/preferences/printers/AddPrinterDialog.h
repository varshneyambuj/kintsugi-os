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
 * MIT License. Copyright 2001-2007, Haiku.
 * Original authors: Philippe Houdoin, Michael Pfeiffer.
 */

/** @file AddPrinterDialog.h
    @brief Modal dialog for creating a new printer entry. */

#ifndef _ADD_PRINTER_DIALOG_H
#define _ADD_PRINTER_DIALOG_H


#include <Button.h>
#include <String.h>
#include <PopUpMenu.h>
#include <TextControl.h>
#include <Window.h>


/**
 * @brief Modal "Add printer" dialog.
 *
 * Lets the user pick a name, driver, and transport, then asks
 * print_server to create the printer entry. Notifies the parent window
 * via kMsgAddPrinterClosed when the dialog closes.
 */
class AddPrinterDialog : public BWindow {
		typedef BWindow Inherited;
public:
								AddPrinterDialog(BWindow *parent);
		
			void				MessageReceived(BMessage *msg);
			bool				QuitRequested();
	
private:
			enum MessageKind {
				kPrinterSelectedMsg = 'adlg',
				kTransportSelectedMsg,
				kNameChangedMsg,
			};
	
	
			void				_AddPrinter(BMessage *msg);
			void				_StorePrinter(BMessage *msg);
			void				_HandleChangedTransport(BMessage *msg);

			void				_BuildGUI(int stage);
			void				_FillTransportMenu(BMenu *menu);
			void				_FillMenu(BMenu *menu, const char *path,
									uint32 what);
			void				_AddPortSubMenu(BMenu *menu,
									const char *transport, const char *port);
			void 				_Update();
		
			BMessenger			fPrintersPrefletMessenger;

			BTextControl*		fName;
			BPopUpMenu*			fPrinter;
			BPopUpMenu*			fTransport;
			BButton*			fOk;
		
			BString 			fNameText;
			BString 			fPrinterText;
			BString 			fTransportText;
			BString 			fTransportPathText;
};

#endif // _ADD_PRINTER_DIALOG_H
