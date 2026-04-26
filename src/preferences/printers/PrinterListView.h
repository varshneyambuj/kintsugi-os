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

/** @file PrinterListView.h
    @brief List view and row class for installed printers. */

#ifndef _PRINTERS_LISTVIEW_H
#define _PRINTERS_LISTVIEW_H


#include <Directory.h>
#include <Entry.h>
#include <Messenger.h>
#include <ListView.h>
#include <String.h>

#include "FolderWatcher.h"


class SpoolFolder;
class PrinterItem;
class PrinterListView;
class BBitmap;
class PrintersWindow;


/**
 * @brief Cached column-width state shared between PrinterItem rows so
 *        that all rows align horizontally.
 */
struct PrinterListLayoutData
{
	float	fLeftColumnMaximumWidth;
	float	fRightColumnMaximumWidth;
};



/**
 * @brief Single-selection list of installed printers.
 *
 * Watches the user printers directory through a FolderWatcher and
 * forwards selection / invocation messages to the parent window.
 */
class PrinterListView : public BListView, public FolderListener {
public:
								PrinterListView(BRect frame);
								~PrinterListView();

			void				AttachedToWindow();
			bool				QuitRequested();

			void				BuildPrinterList();
			PrinterItem*		SelectedItem() const;
			void				UpdateItem(PrinterItem* item);

			PrinterItem*		ActivePrinter() const;
			void 				SetActivePrinter(PrinterItem* item);

private:
		typedef BListView Inherited;

			void 				_AddPrinter(BDirectory& printer, bool calculateLayout);
			void				_LayoutPrinterItems();
			PrinterItem*		_FindItem(node_ref* node) const;

			void				EntryCreated(node_ref* node,
									entry_ref* entry);
			void				EntryRemoved(node_ref* node);
			void				AttributeChanged(node_ref* node);

			FolderWatcher*		fFolder;
			PrinterItem*		fActivePrinter;
			PrinterListLayoutData	fLayoutData;
};


/**
 * @brief Row in PrinterListView representing one installed printer.
 *
 * Caches the printer's display strings and owns the SpoolFolder that
 * tracks job churn for that printer.
 */
class PrinterItem : public BListItem {
public:
								PrinterItem(PrintersWindow* window,
									const BDirectory& node,
									PrinterListLayoutData& layoutData);
								~PrinterItem();

			void				GetColumnWidth(BView* view, float& leftColumn,
									float& rightColumn);

			void				DrawItem(BView* owner, BRect bounds,
									bool complete);
			void				Update(BView* owner, const BFont* font);

			bool				Remove(BListView* view);
			bool				IsActivePrinter() const;
			bool				HasPendingJobs() const;

			/** @brief Returns the printer's display name. */
			const char* 		Name() const { return fName.String(); }
			/** @brief Returns the driver name shown to the user. */
			const char*			Driver() const { return fDriverName.String(); }
			/** @brief Returns the transport name (e.g. USB Port). */
			const char*			Transport() const { return fTransport.String(); }
			/** @brief Returns the transport-specific address (e.g.
			    /dev/parallel/0). */
			const char*			TransportAddress() const
									{ return fTransportAddress.String(); }

			SpoolFolder* 		Folder() const;
			BDirectory* 		Node();
			void				UpdatePendingJobs();

private:
			void				_GetStringProperty(const char* propName,
									BString& outString);
			BBitmap*			_LoadVectorIcon(const char* resourceName,
									float iconSize);

			SpoolFolder*		fFolder;
			BDirectory			fNode;
			BString				fComments;
			BString				fTransport;
			BString				fTransportAddress;
			BString				fDriverName;
			BString				fName;
			BString				fPendingJobs;
			PrinterListLayoutData& fLayoutData;

	static	BBitmap*			sIcon;
	static	BBitmap*			sSelectedIcon;
};

#endif // _PRINTERS_LISTVIEW_H
