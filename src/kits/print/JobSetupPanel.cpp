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
 *   Copyright 2008-2009, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors: Julun, <host.haiku@gmx.de>
 */


/**
 * @file JobSetupPanel.cpp
 * @brief Print-job configuration dialog for the print kit.
 *
 * BJobSetupPanel presents the standard "Print document" dialog allowing the
 * user to select a printer, set the page range, specify the number of copies,
 * and toggle options such as collation, reverse printing, color, and duplex.
 * It delegates to BPrinterRoster for printer enumeration and wraps the result
 * in a BPrintPanel modal window loop.
 *
 * @see BPrintPanel, BPrinterRoster, BPrinter
 */


#include <JobSetupPanel.h>

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <GridLayoutBuilder.h>
#include <GroupLayoutBuilder.h>
#include <GroupView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Printer.h>
#include <PrinterRoster.h>
#include <RadioButton.h>
#include <StringView.h>
#include <TextControl.h>


#include <stdlib.h>


namespace BPrivate {
	namespace Print {


/**
 * @brief Constructs a BJobSetupPanel with default option flags.
 *
 * Initialises internal state and builds the user interface using the default
 * B_NO_OPTIONS flag set.
 *
 * @param printer  The BPrinter to configure; must not be NULL.
 */
BJobSetupPanel::BJobSetupPanel(BPrinter* printer)
	: BPrintPanel("Print document")
	, fPrinter(printer)
	, fPrinterRoster(NULL)
	, fPrintRange(B_ALL_PAGES)
	, fJobPanelFlags(B_NO_OPTIONS)
{
	_InitObject();
	_SetupInterface();
}


/**
 * @brief Constructs a BJobSetupPanel with a custom set of option flags.
 *
 * Identical to the single-argument constructor except that \a flags controls
 * which optional controls (page range, selection, collate, print-to-file) are
 * initially enabled in the dialog.
 *
 * @param printer  The BPrinter to configure; must not be NULL.
 * @param flags    Bitfield of B_PRINT_* option constants.
 */
BJobSetupPanel::BJobSetupPanel(BPrinter* printer, uint32 flags)
	: BPrintPanel("Print document")
	, fPrinter(printer)
	, fPrinterRoster(NULL)
	, fPrintRange(B_ALL_PAGES)
	, fJobPanelFlags(flags)
{
	_InitObject();
	_SetupInterface();
}


/**
 * @brief Destroys the BJobSetupPanel and releases owned resources.
 *
 * Deletes the BPrinterRoster allocated during initialisation.
 */
BJobSetupPanel::~BJobSetupPanel()
{
	delete fPrinterRoster;
}


/**
 * @brief Archive-reconstruction constructor (not yet implemented).
 *
 * @param data  The BMessage archive to restore from.
 */
BJobSetupPanel::BJobSetupPanel(BMessage* data)
	: BPrintPanel(data)
{
	// TODO: implement
}


/**
 * @brief Instantiates a BJobSetupPanel from an archive (not yet implemented).
 *
 * @param data  The BMessage archive produced by Archive().
 * @return Always returns NULL until implemented.
 */
BArchivable*
BJobSetupPanel::Instantiate(BMessage* data)
{
	// TODO: implement
	return NULL;
}


/**
 * @brief Archives this panel into a BMessage (not yet implemented).
 *
 * @param data  The BMessage to write the archive into.
 * @param deep  If true, child views are archived recursively.
 * @return Always returns B_ERROR until implemented.
 */
status_t
BJobSetupPanel::Archive(BMessage* data, bool deep) const
{
	// TODO: implement
	return B_ERROR;
}


/**
 * @brief Handles messages sent to this panel, forwarding to the base class.
 *
 * @param message  The incoming BMessage to dispatch.
 */
void
BJobSetupPanel::MessageReceived(BMessage* message)
{

	BPrintPanel::MessageReceived(message);
}


/**
 * @brief Runs the panel modally and applies the user's selections.
 *
 * Shows the panel via ShowPanel(), applies the chosen settings to the printer
 * object on B_OK, then closes and destroys the window.
 *
 * @return B_OK if the user confirmed, B_CANCEL if they dismissed the dialog,
 *         or another error code on failure.
 */
status_t
BJobSetupPanel::Go()
{
	status_t status = ShowPanel();

	if (status == B_OK) {
		// TODO: check if we did work on an real printer
		// TODO: set all selected values on printer object
	}

	if (Lock())
		Quit();

	return status;
}


/**
 * @brief Returns the printer associated with this panel.
 *
 * @return Pointer to the BPrinter passed at construction time.
 */
BPrinter*
BJobSetupPanel::Printer() const
{
	return fPrinter;
}


/**
 * @brief Replaces the printer associated with this panel.
 *
 * @param printer       The new BPrinter to use.
 * @param keepSettings  If true, the existing print settings are preserved;
 *                      otherwise defaults are restored (not yet implemented).
 */
void
BJobSetupPanel::SetPrinter(BPrinter* printer, bool keepSettings)
{
	// TODO: implement
}


/**
 * @brief Returns the currently selected print range.
 *
 * @return One of B_ALL_PAGES, B_SELECTION, or B_PAGE_RANGE.
 */
print_range
BJobSetupPanel::PrintRange() const
{
	return fPrintRange;
}


/**
 * @brief Sets the active print-range radio button and enables related controls.
 *
 * Updates fPrintRange, adjusts the relevant radio button state, and enables
 * any additional UI controls implied by the chosen range (e.g. page-number
 * fields for B_PAGE_RANGE).
 *
 * @param range  The print_range constant to activate.
 */
void
BJobSetupPanel::SetPrintRange(print_range range)
{
	switch (range) {
		case B_ALL_PAGES: {
			fPrintRange = range;
			fPrintAll->SetValue(B_CONTROL_ON);
		}	break;

		case B_SELECTION: {
			fPrintRange = range;
			SetOptionFlags(OptionFlags() | B_PRINT_SELECTION);
			fSelection->SetValue(B_CONTROL_ON);
		}	break;

		case B_PAGE_RANGE: {
			fPrintRange = range;
			SetOptionFlags(OptionFlags() | B_PRINT_PAGE_RANGE);
			fPagesFrom->SetValue(B_CONTROL_ON);
		}	break;
	}
}


/**
 * @brief Returns the first page number entered by the user.
 *
 * Reads the text from the first-page text control and converts it to an
 * integer. Returns 0 if the field is empty.
 *
 * @return The first page number, or 0 if not specified.
 */
int32
BJobSetupPanel::FirstPage() const
{
	BString text(fFirstPage->Text());
	if (text.Length() <= 0)
		return 0;

	return atoi(text.String());
}


/**
 * @brief Returns the last page number entered by the user.
 *
 * Reads the text from the last-page text control and converts it to an
 * integer. Returns LONG_MAX if the field is empty.
 *
 * @return The last page number, or LONG_MAX if not specified.
 */
int32
BJobSetupPanel::LastPage() const
{
	BString text(fLastPage->Text());
	if (text.Length() <= 0)
		return LONG_MAX;

	return atoi(text.String());
}


/**
 * @brief Sets the page range and updates the corresponding text fields.
 *
 * Activates the B_PAGE_RANGE mode and populates the first- and last-page
 * text controls with the supplied values.
 *
 * @param firstPage  The first page of the range to print.
 * @param lastPage   The last page of the range to print.
 */
void
BJobSetupPanel::SetPageRange(int32 firstPage, int32 lastPage)
{
	BString text;
	SetPrintRange(B_PAGE_RANGE);

	text << firstPage;
	fFirstPage->SetText(text.String());

	text << lastPage;
	fLastPage->SetText(text.String());
}


/**
 * @brief Returns the current set of job option flags.
 *
 * @return The bitfield of B_PRINT_* constants currently active.
 */
uint32
BJobSetupPanel::OptionFlags() const
{
	return fJobPanelFlags;
}


/**
 * @brief Applies a new set of option flags and updates the UI accordingly.
 *
 * Enables or disables the print-to-file checkbox, page-range controls,
 * selection radio button, and collate checkbox based on the flags supplied.
 *
 * @param flags  New bitfield of B_PRINT_* option constants to apply.
 */
void
BJobSetupPanel::SetOptionFlags(uint32 flags)
{
	bool value = false;
	if (flags & B_PRINT_TO_FILE)
		value = true;
	fPrintToFile->SetEnabled(value);

	value = false;
	if (flags & B_PRINT_PAGE_RANGE)
		value = true;
	fPagesFrom->SetEnabled(value);
	fFirstPage->SetEnabled(value);
	fLastPage->SetEnabled(value);

	value = false;
	if (flags & B_PRINT_SELECTION)
		value = true;
	fSelection->SetEnabled(value);

	value = false;
	if (flags & B_PRINT_COLLATE_COPIES)
		value = true;
	fCollate->SetEnabled(value);

	fJobPanelFlags = flags;
}


/**
 * @brief Initialises the printer roster and resolves the default printer if needed.
 *
 * Creates a BPrinterRoster, registers this panel as a watcher for printer
 * changes, and populates fPrinter with the system default if fPrinter is
 * not already pointing to a valid printer.
 */
void
BJobSetupPanel::_InitObject()
{
	fPrinterRoster = new BPrinterRoster();
	fPrinterRoster->StartWatching(this);

	if (!fPrinter->IsValid()) {
		BPrinter defaultPrinter;
		fPrinterRoster->GetDefaultPrinter(&defaultPrinter);
		*fPrinter = defaultPrinter;
	}
}


/**
 * @brief Builds and installs the complete dialog user interface.
 *
 * Creates the printer selection box, page-range box, copies box, and other
 * options box, then assembles them into a BGroupView hierarchy that is handed
 * to BPrintPanel::AddPanel().
 */
void
BJobSetupPanel::_SetupInterface()
{
	BGroupView* groupView = new BGroupView(B_VERTICAL, 10.0);

	// printers
	fPrinterPopUp = new BPopUpMenu("");
	fPrinterPopUp->SetRadioMode(true);
	fPrinterMenuField = new BMenuField("", fPrinterPopUp);
	fPrinterMenuField->Menu()->SetLabelFromMarked(true);

	BPrinter printer;
	while (fPrinterRoster->GetNextPrinter(&printer) == B_OK) {
		BMenuItem* item = new BMenuItem(printer.Name().String(), NULL);
		fPrinterPopUp->AddItem(item);
		if (printer == *fPrinter)
			item->SetMarked(true);
	}

	if (fPrinterRoster->CountPrinters() > 0)
		fPrinterPopUp->AddItem(new BSeparatorItem);

	BMenuItem* pdf = new BMenuItem("Save as PDF file" , NULL);
	fPrinterPopUp->AddItem(pdf);
	if (fPrinterPopUp->FindMarked() == NULL)
		pdf->SetMarked(true);

	fProperties = new BButton("Properties" B_UTF8_ELLIPSIS , new BMessage('prop'));
	fPrinterInfo = new BStringView("label", "");
	fPrinterInfo->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	BBox* divider = new BBox(B_FANCY_BORDER, NULL);
	divider->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 1));
	fPrintToFile = new BCheckBox("Print to file");

	BView* view = BGroupLayoutBuilder(B_VERTICAL, 5.0)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 10.0)
			.Add(fPrinterMenuField->CreateMenuBarLayoutItem())
			.Add(fProperties))
		.Add(BGroupLayoutBuilder(B_HORIZONTAL,5.0)
			.Add(new BStringView("label", "Printer info:"))
			.Add(fPrinterInfo))
		.Add(divider)
		.Add(fPrintToFile)
		.SetInsets(10.0, 5.0, 10.0, 5.0);

	BBox *box = new BBox(B_FANCY_BORDER, view);
	box->SetLabel(BGroupLayoutBuilder()
		.Add(new BStringView("", "Printer"))
		.SetInsets(2.0, 0.0, 2.0, 0.0));
	groupView->AddChild(box);

	// page range
	fPrintAll = new BRadioButton("Print all", new BMessage('prrg'));
	fPrintAll->SetValue(B_CONTROL_ON);
	fPagesFrom = new BRadioButton("Pages from:", new BMessage('prrg'));
	fFirstPage = new BTextControl("", "", NULL);
	_DisallowChar(fFirstPage->TextView());
	fLastPage = new BTextControl("to:", "", NULL);
	_DisallowChar(fLastPage->TextView());
	fSelection = new BRadioButton("Print selection", new BMessage('prrg'));

	fFirstPage->CreateLabelLayoutItem();
	view = BGroupLayoutBuilder(B_VERTICAL, 5.0)
		.Add(fPrintAll)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 5.0)
			.Add(fPagesFrom)
			.Add(fFirstPage->CreateTextViewLayoutItem())
			.Add(fLastPage->CreateLabelLayoutItem())
			.Add(fLastPage->CreateTextViewLayoutItem()))
		.Add(fSelection)
		.SetInsets(10.0, 5.0, 10.0, 5.0);

	box = new BBox(B_FANCY_BORDER, view);
	box->SetLabel(BGroupLayoutBuilder()
		.Add(new BStringView("", "Page range"))
		.SetInsets(2.0, 0.0, 2.0, 0.0));

	// copies
	fNumberOfCopies = new BTextControl("Number of copies:", "1", NULL);
	_DisallowChar(fNumberOfCopies->TextView());
	fCollate = new BCheckBox("Collate");
	fReverse = new BCheckBox("Reverse");

	BView* view2 = BGroupLayoutBuilder(B_VERTICAL, 5.0)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL, 5.0)
			.Add(fNumberOfCopies->CreateLabelLayoutItem())
			.Add(fNumberOfCopies->CreateTextViewLayoutItem()))
		.Add(fCollate)
		.Add(fReverse)
		.SetInsets(10.0, 5.0, 10.0, 5.0);

	BBox* box2 = new BBox(B_FANCY_BORDER, view2);
	box2->SetLabel(BGroupLayoutBuilder()
		.Add(new BStringView("", "Copies"))
		.SetInsets(2.0, 0.0, 2.0, 0.0));

	groupView->AddChild(BGroupLayoutBuilder(B_HORIZONTAL, 10.0)
		.Add(box)
		.Add(box2));

	// other
	fColor = new BCheckBox("Print in color");
	fDuplex = new BCheckBox("Double side printing");

	view = BGroupLayoutBuilder(B_VERTICAL, 5.0)
		.Add(fColor)
		.Add(fDuplex)
		.SetInsets(10.0, 5.0, 10.0, 5.0);

	box = new BBox(B_FANCY_BORDER, view);
	box->SetLabel(BGroupLayoutBuilder()
		.Add(new BStringView("", "Other"))
		.SetInsets(2.0, 0.0, 2.0, 0.0));
	groupView->AddChild(box);

	AddPanel(groupView);
	SetOptionFlags(fJobPanelFlags);
}


/**
 * @brief Restricts a BTextView to accept only digit characters.
 *
 * Iterates over all non-digit byte values and calls DisallowChar() on
 * \a textView for each, ensuring the user can only type numeric input.
 *
 * @param textView  The BTextView to configure.
 */
void
BJobSetupPanel::_DisallowChar(BTextView* textView)
{
	for (uint32 i = 0; i < '0'; ++i)
		textView->DisallowChar(i);

	for (uint32 i = '9' + 1; i < 255; ++i)
		textView->DisallowChar(i);
}


	}	// namespace Print
}	// namespace BPrivate
