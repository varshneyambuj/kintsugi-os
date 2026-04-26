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
 *   Copyright 2001-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */


/**
 * @file PrinterListView.cpp
 * @brief BListView and row class showing every installed printer.
 *
 * PrinterListView watches the user printers directory for changes via a
 * FolderWatcher and rebuilds itself whenever printer definition nodes are
 * added, removed or modified. Each row is a PrinterItem with the printer's
 * name, driver, transport and pending-job summary.
 *
 * @see SpoolFolder, FolderWatcher
 */


#include "PrinterListView.h"

#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <IconUtils.h>
#include <Locale.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Resources.h>
#include <String.h>
#include <StringFormat.h>

#include "pr_server.h"
#include "Messages.h"
#include "Globals.h"
#include "PrintersWindow.h"
#include "SpoolFolder.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrinterListView"


// #pragma mark -- PrinterListView


/**
 * @brief Constructs an empty single-selection printer list view.
 *
 * @param frame Initial layout frame.
 */
PrinterListView::PrinterListView(BRect frame)
	: Inherited(frame, "printers_list", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fFolder(NULL),
	fActivePrinter(NULL)
{
	fLayoutData.fLeftColumnMaximumWidth = 100;
	fLayoutData.fRightColumnMaximumWidth = 100;
}


/**
 * @brief Destructor; deletes every owned PrinterItem.
 */
PrinterListView::~PrinterListView()
{
	while (!IsEmpty())
		delete RemoveItem((int32)0);
}


/**
 * @brief Rebuilds the list from the user printers directory.
 *
 * Clears existing entries and walks every node under
 * B_USER_PRINTERS_DIRECTORY adding qualifying printer definitions. Called
 * during initial population and after major folder events.
 */
void
PrinterListView::BuildPrinterList()
{
	// clear list
	while (!IsEmpty())
		delete RemoveItem((int32)0);

	// Find directory containing printer definition nodes
	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while(dir.GetNextEntry(&entry) == B_OK) {
		BDirectory printer(&entry);
		_AddPrinter(printer, false);
	}

	_LayoutPrinterItems();
}


/**
 * @brief Sets up message routing and starts watching the printers
 *        directory.
 *
 * Hooks the selection and invocation messages, creates the printers
 * directory if missing, allocates the FolderWatcher, populates the list,
 * and selects whichever row corresponds to the currently active printer.
 */
void
PrinterListView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	SetSelectionMessage(new BMessage(kMsgPrinterSelected));
	SetInvocationMessage(new BMessage(kMsgMakeDefaultPrinter));
	SetTarget(Window());

	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK) {
		// directory has to exist in order to start watching it
		if (create_directory(path.Path(), 0777) != B_OK)
			return;
		dir.SetTo(path.Path());
	}

	fFolder = new FolderWatcher(Window(), dir, true);
	fFolder->SetListener(this);

	BuildPrinterList();

	// Select active printer
	BString activePrinterName(ActivePrinterName());
	for (int32 i = 0; i < CountItems(); i ++) {
		PrinterItem* item = dynamic_cast<PrinterItem*>(ItemAt(i));
		if (item != NULL && item->Name() == activePrinterName) {
			Select(i);
			fActivePrinter = item;
			break;
		}
	}
}


/**
 * @brief Releases the folder watcher when the window closes.
 *
 * @return Always true; the list view does not block closing.
 */
bool
PrinterListView::QuitRequested()
{
	delete fFolder;
	return true;
}


/**
 * @brief Refreshes a single row after its underlying state changed.
 *
 * Rebuilds its pending-job string and asks the list to redraw it.
 *
 * @param item Row to refresh; must currently be in this list.
 */
void
PrinterListView::UpdateItem(PrinterItem* item)
{
	item->UpdatePendingJobs();
	InvalidateItem(IndexOf(item));
}


/**
 * @brief Returns the row tagged as the active printer.
 *
 * @return PrinterItem corresponding to the system default, or NULL when
 *         none has been identified yet.
 */
PrinterItem*
PrinterListView::ActivePrinter() const
{
	return fActivePrinter;
}


/**
 * @brief Updates the cached pointer to the active printer row.
 *
 * @param item PrinterItem for the new active printer; may be NULL.
 */
void
PrinterListView::SetActivePrinter(PrinterItem* item)
{
	fActivePrinter = item;
}


/**
 * @brief Returns the currently selected printer row.
 *
 * @return Selected PrinterItem, or NULL if nothing is selected.
 */
PrinterItem*
PrinterListView::SelectedItem() const
{
	return dynamic_cast<PrinterItem*>(ItemAt(CurrentSelection()));
}


// FolderListener interface

/**
 * @brief FolderListener callback for new printer entries.
 *
 * @param node  Node reference of the newly created directory.
 * @param entry Entry reference (currently unused).
 */
void
PrinterListView::EntryCreated(node_ref* node, entry_ref* entry)
{
	BDirectory printer(node);
	_AddPrinter(printer, true);
}


/**
 * @brief FolderListener callback for removed printer entries.
 *
 * @param node Node reference of the directory that disappeared.
 */
void
PrinterListView::EntryRemoved(node_ref* node)
{
	PrinterItem* item = _FindItem(node);
	if (item) {
		if (item == fActivePrinter)
			fActivePrinter = NULL;

		RemoveItem(item);
		delete item;
	}
}


/**
 * @brief FolderListener callback for printer attribute changes.
 *
 * Re-evaluates the directory in case the change made a previously
 * non-printer node qualify (or vice versa).
 *
 * @param node Node reference whose attributes changed.
 */
void
PrinterListView::AttributeChanged(node_ref* node)
{
	BDirectory printer(node);
	_AddPrinter(printer, true);
}


// private methods

/**
 * @brief Adds @a printer to the list if it is a printer-definition node.
 *
 * Checks the directory's MIME type, state attribute, and ensures it is not
 * already represented before allocating a new PrinterItem.
 *
 * @param printer         Candidate directory.
 * @param calculateLayout When true, re-run column-width layout after
 *                        adding.
 */
void
PrinterListView::_AddPrinter(BDirectory& printer, bool calculateLayout)
{
	BString state;
	node_ref node;
		// If the entry is a directory
	if (printer.InitCheck() == B_OK
		&& printer.GetNodeRef(&node) == B_OK
		&& _FindItem(&node) == NULL
		&& printer.ReadAttrString(PSRV_PRINTER_ATTR_STATE, &state) == B_OK
		&& state == "free") {
			// Check it's Mime type for a spool director
		BNodeInfo info(&printer);
		char buffer[256];

		if (info.GetType(buffer) == B_OK
			&& strcmp(buffer, PSRV_PRINTER_FILETYPE) == 0) {
				// Yes, it is a printer definition node
			AddItem(new PrinterItem(static_cast<PrintersWindow*>(Window()),
				printer, fLayoutData));
			if (calculateLayout)
				_LayoutPrinterItems();
		}
	}
}


/**
 * @brief Recomputes the maximum left and right column widths.
 *
 * Walks every PrinterItem to find the widest name/driver and pending-jobs
 * /transport strings, stores the results in fLayoutData, and asks for a
 * redraw so the new widths take effect.
 */
void
PrinterListView::_LayoutPrinterItems()
{
	float& leftColumnMaximumWidth = fLayoutData.fLeftColumnMaximumWidth;
	float& rightColumnMaximumWidth = fLayoutData.fRightColumnMaximumWidth;

	for (int32 i = 0; i < CountItems(); i ++) {
		PrinterItem* item = static_cast<PrinterItem*>(ItemAt(i));

		float leftColumnWidth = 0;
		float rightColumnWidth = 0;
		item->GetColumnWidth(this, leftColumnWidth, rightColumnWidth);

		leftColumnMaximumWidth = MAX(leftColumnMaximumWidth,
			leftColumnWidth);
		rightColumnMaximumWidth = MAX(rightColumnMaximumWidth,
			rightColumnWidth);
	}

	Invalidate();
}


/**
 * @brief Looks up the row whose underlying directory matches @a node.
 *
 * @param node Node reference to compare against each item.
 *
 * @return Matching PrinterItem or NULL if none is present.
 */
PrinterItem*
PrinterListView::_FindItem(node_ref* node) const
{
	for (int32 i = CountItems() - 1; i >= 0; i--) {
		PrinterItem* item = dynamic_cast<PrinterItem*>(ItemAt(i));
		node_ref ref;
		if (item && item->Node()->GetNodeRef(&ref) == B_OK && ref == *node)
			return item;
	}
	return NULL;
}



// #pragma mark -- PrinterItem


/** @brief Cached generic printer icon shared by every PrinterItem. */
BBitmap* PrinterItem::sIcon = NULL;
/** @brief Cached printer icon with a check-mark overlay used for the
    active printer row. */
BBitmap* PrinterItem::sSelectedIcon = NULL;


/**
 * @brief Constructs a row tied to a printer-definition directory.
 *
 * Lazily creates the shared sIcon and sSelectedIcon bitmaps the first
 * time, reads the printer name, comments, transport, address and driver
 * attributes off @a node, then creates a SpoolFolder watcher rooted at
 * the printer's spool directory.
 *
 * @param window     Owning PrintersWindow (for spool-folder callbacks).
 * @param node       Printer-definition directory.
 * @param layoutData Shared layout struct holding column-width caches.
 */
PrinterItem::PrinterItem(PrintersWindow* window, const BDirectory& node,
		PrinterListLayoutData& layoutData)
	: BListItem(0, false),
	fFolder(NULL),
	fNode(node),
	fLayoutData(layoutData)
{
	BRect rect(BPoint(0, 0), be_control_look->ComposeIconSize(B_LARGE_ICON));
	if (sIcon == NULL) {
		sIcon = new BBitmap(rect, B_RGBA32);
		BMimeType type(PSRV_PRINTER_FILETYPE);
		type.GetIcon(sIcon, (icon_size)(rect.IntegerHeight() + 1));
	}

	if (sIcon && sIcon->IsValid() && sSelectedIcon == NULL) {
		const float checkMarkIconSize = be_control_look->ComposeIconSize(20).Height();
		BBitmap *checkMark = _LoadVectorIcon("check_mark_icon",
			checkMarkIconSize);
		if (checkMark && checkMark->IsValid()) {
			sSelectedIcon = new BBitmap(rect, B_RGBA32, true);
			if (sSelectedIcon && sSelectedIcon->IsValid()) {
				// draw check mark at bottom left over printer icon
				BView *view = new BView(rect, "offscreen", B_FOLLOW_ALL,
					B_WILL_DRAW);
				float y = rect.Height() - checkMark->Bounds().Height();
				sSelectedIcon->Lock();
				sSelectedIcon->AddChild(view);
				view->DrawBitmap(sIcon);
				view->SetDrawingMode(B_OP_ALPHA);
				view->DrawBitmap(checkMark, BPoint(0, y));
				view->Sync();
				view->RemoveSelf();
				sSelectedIcon->Unlock();
				delete view;
			}
		}
		delete checkMark;
	}

	// Get Name of printer
	_GetStringProperty(PSRV_PRINTER_ATTR_PRT_NAME, fName);
	_GetStringProperty(PSRV_PRINTER_ATTR_COMMENTS, fComments);
	_GetStringProperty(PSRV_PRINTER_ATTR_TRANSPORT, fTransport);
	_GetStringProperty(PSRV_PRINTER_ATTR_TRANSPORT_ADDR, fTransportAddress);
	_GetStringProperty(PSRV_PRINTER_ATTR_DRV_NAME, fDriverName);

	BPath path;
	if (find_directory(B_USER_PRINTERS_DIRECTORY, &path) != B_OK)
		return;

	// Setup spool folder
	path.Append(fName.String());
	BDirectory dir(path.Path());
	if (dir.InitCheck() == B_OK) {
		fFolder = new SpoolFolder(window, this, dir);
		UpdatePendingJobs();
	}
}


/**
 * @brief Destructor; releases the SpoolFolder.
 */
PrinterItem::~PrinterItem()
{
	delete fFolder;
}


/**
 * @brief Reports the widths needed for the left and right text columns.
 *
 * Used by PrinterListView::_LayoutPrinterItems() to keep all rows aligned.
 *
 * @param view        View used for font metrics.
 * @param leftColumn  Output: maximum width of the left-hand columns.
 * @param rightColumn Output: maximum width of the right-hand columns.
 */
void
PrinterItem::GetColumnWidth(BView* view, float& leftColumn, float& rightColumn)
{
	BFont font;
	view->GetFont(&font);

	leftColumn = font.StringWidth(fName.String());
	leftColumn = MAX(leftColumn, font.StringWidth(fDriverName.String()));

	rightColumn = font.StringWidth(fPendingJobs.String());
	rightColumn = MAX(rightColumn, font.StringWidth(fTransport.String()));
	rightColumn = MAX(rightColumn, font.StringWidth(fComments.String()));
}


/**
 * @brief Computes row height from font metrics, leaving room for three
 *        text lines plus padding.
 *
 * @param owner View that owns the row.
 * @param font  Font supplying the metrics.
 */
void
PrinterItem::Update(BView *owner, const BFont *font)
{
	BListItem::Update(owner,font);

	font_height height;
	font->GetHeight(&height);

	SetHeight((height.ascent + height.descent + height.leading) * 3.0 + 8.0);
}


/**
 * @brief Asks print_server to delete the printer this row represents.
 *
 * Sends a B_DELETE_PROPERTY scripting message specifying the row's index
 * inside @a view.
 *
 * @param view List view used to compute the printer's index.
 *
 * @return True when print_server acknowledged the request, false otherwise.
 */
bool PrinterItem::Remove(BListView* view)
{
	BMessenger msgr;
	if (GetPrinterServerMessenger(msgr) == B_OK) {
		BMessage script(B_DELETE_PROPERTY);
		script.AddSpecifier("Printer", view->IndexOf(this));

		BMessage reply;
		if (msgr.SendMessage(&script,&reply) == B_OK)
			return true;
	}
	return false;
}


/**
 * @brief Renders the printer row.
 *
 * Paints the selection background, draws the printer icon (with a
 * check-mark overlay when this is the active printer), and lays out three
 * lines of text: name + pending jobs, driver + comments, and transport
 * details.
 *
 * @param owner    BListView doing the drawing.
 * @param complete Forwarded by the base class; unused here.
 */
void
PrinterItem::DrawItem(BView *owner, BRect /*bounds*/, bool complete)
{
	BListView* list = dynamic_cast<BListView*>(owner);
	if (list == NULL)
		return;

	BFont font;
	owner->GetFont(&font);

	font_height height;
	font.GetHeight(&height);

	float fntheight = height.ascent + height.descent + height.leading;

	BRect bounds = list->ItemFrame(list->IndexOf(this));

	rgb_color color = owner->ViewColor();
	rgb_color oldLowColor = owner->LowColor();
	rgb_color oldHighColor = owner->HighColor();

	if (IsSelected())
		color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

	owner->SetLowColor(color);
	owner->SetHighColor(color);

	owner->FillRect(bounds);

	owner->SetLowColor(oldLowColor);
	owner->SetHighColor(oldHighColor);

	float iconColumnWidth = B_LARGE_ICON + 8.0;
	if (sIcon)
		iconColumnWidth = sIcon->Bounds().Height() + 8.0;
	float x = iconColumnWidth;
	BPoint iconPt(bounds.LeftTop() + BPoint(2.0, 2.0));
	BPoint namePt(iconPt + BPoint(x, fntheight));
	BPoint driverPt(iconPt + BPoint(x, fntheight * 2.0));
	BPoint defaultPt(iconPt + BPoint(x, fntheight * 3.0));
	BPoint transportPt(iconPt + BPoint(x, fntheight * 3.0));

	float totalWidth = bounds.Width() - iconColumnWidth;
	float maximumWidth = fLayoutData.fLeftColumnMaximumWidth +
		fLayoutData.fRightColumnMaximumWidth;
	float width;
	if (totalWidth < maximumWidth) {
		width = fLayoutData.fRightColumnMaximumWidth * totalWidth /
			maximumWidth;
	} else {
		width = fLayoutData.fRightColumnMaximumWidth;
	}

	BPoint pendingPt(bounds.right - width - 8.0, namePt.y);
	BPoint commentPt(bounds.right - width - 8.0, driverPt.y);


	drawing_mode mode = owner->DrawingMode();
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	owner->SetDrawingMode(B_OP_ALPHA);
#else
	owner->SetDrawingMode(B_OP_OVER);
#endif
	if (IsActivePrinter()) {
		if (sSelectedIcon && sSelectedIcon->IsValid())
			owner->DrawBitmap(sSelectedIcon, iconPt);
		else
			owner->DrawString(B_TRANSLATE("Default Printer"), defaultPt);
	} else {
		if (sIcon && sIcon->IsValid())
			owner->DrawBitmap(sIcon, iconPt);
	}

	owner->SetDrawingMode(B_OP_OVER);

	// left of item
	BString s = fName;
	owner->SetFont(be_bold_font);
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, pendingPt.x - namePt.x);
	owner->DrawString(s.String(), s.Length(), namePt);
	owner->SetFont(&font);

	s = B_TRANSLATE("Driver: %driver%");
	s.ReplaceFirst("%driver%", fDriverName);
	owner->TruncateString(&s, B_TRUNCATE_END, commentPt.x - driverPt.x);
	owner->DrawString(s.String(), s.Length(), driverPt);


	if (fTransport.Length() > 0) {
		s = B_TRANSLATE("Transport: %transport% %transport_address%");
		s.ReplaceFirst("%transport%", fTransport);
		s.ReplaceFirst("%transport_address%", fTransportAddress);
		owner->TruncateString(&s, B_TRUNCATE_BEGINNING, totalWidth);
		owner->DrawString(s.String(), s.Length(), transportPt);
	}

	// right of item
	s = fPendingJobs;
	owner->TruncateString(&s, B_TRUNCATE_END, bounds.Width() - pendingPt.x);
	owner->DrawString(s.String(), s.Length(), pendingPt);

	s = fComments;
	owner->TruncateString(&s, B_TRUNCATE_MIDDLE, bounds.Width() - commentPt.x);
	owner->DrawString(s.String(), s.Length(), commentPt);

	owner->SetDrawingMode(mode);
}


/**
 * @brief Returns true when this row represents the system default
 *        printer.
 *
 * @return True if the row's name matches print_server's active printer.
 */
bool
PrinterItem::IsActivePrinter() const
{
	return fName == ActivePrinterName();
}


/**
 * @brief Returns whether the row has any spooled jobs.
 *
 * @return True when at least one job exists in the printer's spool
 *         folder.
 */
bool
PrinterItem::HasPendingJobs() const
{
	return fFolder && fFolder->CountJobs() > 0;
}


/**
 * @brief Returns the SpoolFolder backing this printer.
 *
 * @return Pointer owned by this item, or NULL if the spool directory
 *         could not be opened.
 */
SpoolFolder*
PrinterItem::Folder() const
{
	return fFolder;
}


/**
 * @brief Returns the BDirectory wrapping this printer's definition node.
 *
 * @return Pointer to the cached BDirectory; valid for the lifetime of the
 *         item.
 */
BDirectory*
PrinterItem::Node()
{
	return &fNode;
}


/**
 * @brief Re-counts spooled jobs and updates the cached display string.
 *
 * The string uses a localized BStringFormat plural pattern so the row
 * can show "No pending jobs", "1 pending job", or "N pending jobs".
 */
void
PrinterItem::UpdatePendingJobs()
{
	uint32 pendingJobs = 0;
	if (fFolder)
		pendingJobs = fFolder->CountJobs();

	static BStringFormat format(B_TRANSLATE("{0, plural,"
		"=0{No pending jobs}"
		"=1{1 pending job}"
		"other{# pending jobs}}"));

	format.Format(fPendingJobs, pendingJobs);
}


/**
 * @brief Reads a string attribute from the printer definition node.
 *
 * @param propName  Attribute name to read.
 * @param outString Output BString receiving the value (untouched on
 *                  failure).
 */
void
PrinterItem::_GetStringProperty(const char* propName, BString& outString)
{
	fNode.ReadAttrString(propName, &outString);
}


/**
 * @brief Loads and rasterises a vector icon resource from the
 *        application's resources.
 *
 * @param resourceName Name of the B_VECTOR_ICON_TYPE resource.
 * @param iconSize     Side length, in pixels, of the requested raster
 *                     bitmap.
 *
 * @return Newly allocated BBitmap (caller takes ownership) or NULL when
 *         the resource is missing or rasterisation fails.
 */
BBitmap*
PrinterItem::_LoadVectorIcon(const char* resourceName, float iconSize)
{
	size_t dataSize;
	BResources* resources = BApplication::AppResources();
	const void* data = resources->LoadResource(B_VECTOR_ICON_TYPE,
		resourceName, &dataSize);

	if (data != NULL){
		BBitmap *iconBitmap = new BBitmap(BRect(0, 0, iconSize - 1,
			iconSize - 1), 0, B_RGBA32);
		if (BIconUtils::GetVectorIcon(
				reinterpret_cast<const uint8*>(data),
				dataSize, iconBitmap) == B_OK)
			return iconBitmap;
		else
			delete iconBitmap;
	};
	return NULL;
}
